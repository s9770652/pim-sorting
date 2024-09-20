/**
 * @file
 * @brief Parallel MergeSort based on Cormen et al. ‘Algorithmen – Eine Einführung’, 4th ed.
**/
#include <stdlib.h>
#include <string.h>

#include <barrier.h>
#include <defs.h>
#include <handshake.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "checkers.h"
#include "communication.h"
#include "mram_merging.h"
#include "mram_sorts.h"
#include "random_distribution.h"
#include "reader.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot
array_stats stats_before, stats_after;
dpu_time times[NR_TASKLETS];

BARRIER_INIT(omni_barrier, NR_TASKLETS);

seqreader_t sr[NR_TASKLETS][2];  // sequential readers used to read runs
bool flipped[NR_TASKLETS];  // Whether `output` contains the latest sorted runs.
mram_range from[NR_TASKLETS][2];
size_t borders[NR_TASKLETS];

/**
 * @brief Finds the *greatest* index 𝘪 ∈ [`start`, `end`] such that `array[𝘪 – 1]` < `to_find`.
 * If the array is empty, `start` is returned. If `to_find` ≦ `array[start]`, `start` is returned.
 * 
 * @param to_find The element for which to find the next less element.
 * @param array The array where to search for the next less element.
 * @param start The index of the first element to consider.
 * @param end The index of the last element to consider.
 * 
 * @return The index of the next less element or, if none exists, `start`.
**/
size_t binary_search_strict(T const to_find, T __mram_ptr *array, size_t start, size_t end) {
    if (end < start) return start;
    size_t left = start, right = end + 1;
    while (left < right) {
        size_t const middle = (left + right) / 2;  // No overflow due to the small MRAM.
        if (to_find <= array[middle])
            right = middle;
        else
            left = middle + 1;
    }
    return right;
}

/**
 * @brief Finds *some* index 𝘪 ∈ [`start`, `end`] such that `array[𝘪]` ≦ `to_find`.
 * If the array is empty, `start` is returned. If `to_find` ≦ `array[start]`, `start` is returned.
 * 
 * @param to_find The element for which to find a less element.
 * @param array The array where to search for a less element.
 * @param start The index of the first element to consider.
 * @param end The index of the last element to consider.
 * 
 * @return The index of a less element or, if none exists, `start`.
**/
size_t binary_search_loose(T const to_find, T __mram_ptr *array, size_t start, size_t end) {
    if (end < start) return start;
    size_t left = start, right = end + 1;
    while (left < right) {
        size_t const middle = (left + right) / 2;  // No overflow due to the small MRAM.
        if (to_find == array[middle])
            return middle;
        else if (to_find < array[middle])
            right = middle;
        else
            left = middle + 1;
    }
    return right;
}

/**
 * @brief A strict binary search if `STABLE` is set to `true`, and a loose one elsewise.
 * 
 * @param to_find The element for which to find some less element.
 * @param array The array where to search for some less element.
 * @param start The index of the first element to consider.
 * @param end The index of the last element to consider.
 * 
 * @return The index of some less element or, if none exists, `start`.
**/
size_t binary_search(T const to_find, T __mram_ptr *array, size_t start, size_t end) {
#if (STABLE)
    return binary_search_strict(to_find, array, start, end);
#else
    return binary_search_loose(to_find, array, start, end);
#endif
}

/**
 * @brief Given `NR_TASKLETS` sorted MRAM runs, stored in from[…][0],
 * this function performs a parallel MergeSort based on a scheme by Cormen et al.
**/
static __attribute__((unused)) void merge_par(void) {
    seqreader_buffer_t const wram[2] = { buffers[me()].seq_1, buffers[me()].seq_2 };
    unsigned char const trailing_zeros = (me() == 0) ? 32 : __builtin_ctz(me());
    for (sysname_t round = 1; (1 << round) <= NR_TASKLETS; round++) {
        T __mram_ptr * const in = (flipped[me()]) ? output : input;
        T __mram_ptr * const out = (flipped[me()]) ? input : output;
        sysname_t I = me();
        // Are the `sub_round` LSB are all zero?
        if (!(I & ((1 << round) - 1))) {
            // If so, I am a root tasklet and have to wait for the tasklets within my tree.
            for (sysname_t i = 1; i < (1 << round); i++) {
                handshake_wait_for(I + i);
            }
            from[I][1] = from[I | (1 << (round - 1))][0];
        } else {
            // If not, I am an inner tasklet and have to wait for my root to wake me up.
            handshake_notify();
            handshake_notify();
        }
        // I have been awoken and wake now sequentially the tasklets within my subtree,
        // that is those with less zeroes in their LSB.
        for (
            sysname_t sub_round = (trailing_zeros > round) ? round : trailing_zeros;
            sub_round >= 1;
            sub_round--
        ) {
            sysname_t const thou = I ^ (1 << (sub_round - 1));
            // Calculating the division points.
            mram_range runs[2];  // 0: shorter; 1: longer
            if ((ptrdiff_t)(from[I][0].end - from[I][0].start) <=
                    (ptrdiff_t)(from[I][1].end - from[I][1].start)) {
                runs[0] = from[I][0];
                runs[1] = from[I][1];
            } else {
                runs[0] = from[I][0];
                runs[1] = from[I][1];
            }
            size_t pivot = (runs[1].start + runs[1].end) / 2;
            T const pivot_value = in[pivot];
#if STABLE
            if (in[pivot - 1] == pivot_value)  // Are there even duplicates to find?
                pivot = binary_search(pivot_value, in, runs[1].start, pivot - 1);
#endif
            size_t const cut_at = binary_search(pivot_value, in, runs[0].start, runs[0].end);
            size_t const border = borders[I] + (pivot - runs[1].start) + (cut_at - runs[0].start);
            out[border] = pivot_value;
            // Telling thee thy sections and awakening thee.
            from[thou][0].start = cut_at;
            from[thou][0].end = runs[0].end;
            from[thou][1].start = pivot + 1;
            from[thou][1].end = runs[1].end;
            borders[thou] = border + 1;
            handshake_wait_for(thou);
            // Saving mine own sections for either further division or for sorting, finally.
            from[I][0].start = runs[0].start;
            from[I][0].end = cut_at - 1;
            from[I][1].start = runs[1].start;
            from[I][1].end = pivot - 1;
        }
        // All tasklets within my subtree are awake, so I can process my two runs.
        T __mram_ptr * const starts[2] = { &in[from[I][0].start], &in[from[I][1].start] };
        T __mram_ptr * const ends[2] = { &in[from[I][0].end], &in[from[I][1].end] };
        if ((intptr_t)starts[0] > (intptr_t)ends[0]) {  // The shorter run may be empty.
            size_t offset = 0;
#if UINT32
            if ((uintptr_t)&out[borders[I]] & DMA_OFF_MASK) {
                atomic_write(&out[borders[I]], *starts[1]);
                offset = 1;
            }
#endif
            flush_run(starts[1] + offset, ends[1], &out[borders[I] + offset]);
        } else {
            T *ptr[2] = {
                sr_init(buffers[I].seq_1, starts[0], &sr[I][0]),
                sr_init(buffers[I].seq_2, starts[1], &sr[I][1]),
            };
            merge_mram(ptr, ends, &out[borders[I]], wram);
        }
        flipped[I] = !flipped[I];
        // Now, I need to calculate the boundaries of the sorted run of my subtree.
        // The root wrote to the head, the rightmost leaf to the tail.
        if (!(I & ((1 << round) - 1))) {  // Am I the root?
            from[I][0].start = borders[I];
            sysname_t const rightmost_leaf = I | ((1 << round) - 1);
            handshake_wait_for(rightmost_leaf);
            size_t const length = from[rightmost_leaf][0].end - from[rightmost_leaf][0].start + 1 +
                    from[rightmost_leaf][1].end - from[rightmost_leaf][1].start + 1;
            from[I][0].end = borders[rightmost_leaf] + length - 1;
        } else if ((I & ((1 << round) - 1)) == ((1 << round) - 1)) {  // Am I the rightmost leaf?
            handshake_notify();
        }
    }
}

/**
 * @brief Forms `NR_TASKLETS` starting runs, ensures that everything is within the same array and,
 * then, merges in parallel.
 * 
 * @param start The first element to sort by the calling tasklet.
 * @param end The last element to sort by the calling tasklet.
**/
static void merge_sort_par(T __mram_ptr * const start, T __mram_ptr * const end) {
    merge_sort_mram(start, end);
#if (NR_TASKLETS > 1)
    if (me() == NR_TASKLETS - 2) {
        handshake_notify();
        handshake_wait_for(me() + 1);
    } else if (me() == NR_TASKLETS - 1) {
        handshake_wait_for(me() - 1);
        if (flipped[me() - 1] != flipped[me()]) {
            T __mram_ptr *in = (flipped[me()]) ? output : input;
            T __mram_ptr *out = (flipped[me()]) ? input : output;
            copy_run(&in[from[me()][0].start], &in[from[me()][0].end], &out[from[me()][0].start]);
            flipped[me()] = !flipped[me()];
        }
        handshake_notify();
    }
    borders[me()] = from[me()][0].start;
    merge_par();
#endif  // NR_TASKLETS > 1
}

union algo_to_test __host algos[] = {
    {{ "MergePar", { .mram = merge_sort_par } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (me() == 0 && host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 0x700000;
        host_to_dpu.offset = DMA_ALIGNED(host_to_dpu.length * sizeof(T)) / sizeof(T);
        host_to_dpu.part_length =
                DMA_ALIGNED(DIV_CEIL(host_to_dpu.length, NR_TASKLETS) * sizeof(T)) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }
    barrier_wait(&omni_barrier);

    /* Perform test. */
    mram_range range = {
        me() * host_to_dpu.part_length,
        (me() == NR_TASKLETS - 1) ? host_to_dpu.offset : (me() + 1) * host_to_dpu.part_length,
    };
    from[me()][0].start = range.start, from[me()][0].end = range.end - 1;
    sort_algo_mram * const algo = algos[host_to_dpu.algo_index].data.fct.mram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());

        get_stats_unsorted(input, cache, range, false, &stats_before);

        barrier_wait(&omni_barrier);
        perfcounter_config(COUNT_CYCLES, true);
        dpu_time new_time = perfcounter_get();
        algo(&input[range.start], &input[range.end - 1]);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD_CYCLES;
        times[me()] = new_time;
        barrier_wait(&omni_barrier);
        if (me() == 0) {
            for (size_t i = 1; i < NR_TASKLETS; i++)
                times[0] += times[i];
            dpu_to_host.firsts += times[0];
            dpu_to_host.seconds += times[0] * times[0];
        }

        T __mram_ptr *sorted_array = (flipped[me()]) ? output : input;
        flipped[me()] = false;
        get_stats_sorted(sorted_array, cache, range, false, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        range.start += host_to_dpu.offset;
        range.end += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
