/**
 * @file
 * @brief Measuring runtimes of a half-space MergeSort (sequential, MRAM, regular readers).
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <alloc.h>
#include <defs.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "buffers.h"
#include "checkers.h"
#include "common.h"
#include "communication.h"
#include "random_distribution.h"
#include "random_generator.h"
#include "wram_sorts.h"

#include "seqreader_straight.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

/// @brief The number of items in the starting runs.
#define STARTING_RUN_LENGTH (TRIPLE_BUFFER_LENGTH)
/// @brief The number of bytes a starting run takes.
#define STARTING_RUN_SIZE (STARTING_RUN_LENGTH << DIV)
static_assert(
    STARTING_RUN_SIZE == DMA_ALIGNED(STARTING_RUN_SIZE),
    "The size of starting runs must be properly aligned for DMAs!"
);
static_assert(
    STARTING_RUN_SIZE <= TRIPLE_BUFFER_SIZE,
    "The starting runs are sorted entirely in WRAM and, thus, must fit in there!"
);

/// @brief How many items are merged in an unrolled fashion.
#define UNROLL_FACTOR (16)
/// @brief How many items the cache holds before they are written to the MRAM.
/// @internal Despite the unrolling, medium sizes are worse than the maximum size.
#define UNROLLING_CACHE_LENGTH (MIN(256, MAX_TRANSFER_LENGTH_CACHE) / UNROLL_FACTOR * UNROLL_FACTOR)
/// @brief How many bytes the items the cache holds before they are written to the MRAM have.
#define UNROLLING_CACHE_SIZE (UNROLLING_CACHE_LENGTH << DIV)

/**
 * @brief Scans an MRAM array backwards in blocks of length `STARTING_RUN_LENGTH`,
 * sorts those in WRAM, and writes them back.
 * 
 * @param start The first item of the MRAM array to sort.
 * @param end The last item of said array.
**/
static void form_starting_runs(T __mram_ptr * const start, T __mram_ptr * const end) {
    T * const cache = buffers[me()].cache;
    T __mram_ptr *i;
    size_t curr_length, curr_size;
    mram_range_ptr range = { start, end + 1 };
    LOOP_BACKWARDS_ON_MRAM_BL(i, curr_length, curr_size, range, STARTING_RUN_LENGTH) {
#if (STARTING_RUN_SIZE > 2048)
        mram_read_triple(i, cache, curr_size);
        quick_sort_wram(cache, cache + curr_length - 1);
        mram_write_triple(cache, i, curr_size);
#else
        mram_read(i, cache, curr_size);
        quick_sort_wram(cache, cache + curr_length - 1);
        mram_write(cache, i, curr_size);
#endif
    }
}

/**
 * @brief Copies a sorted MRAM array to another MRAM location.
 * 
 * @param from The first item of the MRAM array to copy.
 * @param to The last item of said array.
 * @param out The new location of the first item to copy.
**/
static void copy_run(T __mram_ptr *from, T __mram_ptr *to, T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    mram_range_ptr range = { from, to + 1 };
    T __mram_ptr *i;
    size_t curr_length, curr_size;
    LOOP_ON_MRAM_BL(i, curr_length, curr_size, range, MAX_TRANSFER_LENGTH_TRIPLE) {
        mram_read(i, cache, curr_size);
        mram_write(cache, out, curr_size);
        out += curr_length;
    }
}

/**
 * @brief Flushes the first run in a pair of runs once the tail of the second one is reached.
 * This includes writing whatever is still in the cache to the MRAM
 * and copying whatever is still in the copy of the first run back to the input array.
 * 
 * @param ptr The current buffer item of the first run.
 * @param from The MRAM address of the current item of the first run.
 * @param to The MRAM address of the last item of the first run.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static void flush_first(T const * const ptr, T __mram_ptr *from, T __mram_ptr const *to,
        T __mram_ptr *out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
    /* Transfer cache to MRAM. */
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = *ptr;
        if (from >= to) {
            mram_write(cache, out, i * sizeof(T));
            return;
        }
        from++;
    }
#endif
    mram_write(cache, out, i * sizeof(T));
    out += i;

    /* Transfer from MRAM to MRAM. */
    do {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        size_t const rem_size = (from + MAX_TRANSFER_LENGTH_TRIPLE > to)
                ? (size_t)to - (size_t)from + sizeof(T)
                : MAX_TRANSFER_SIZE_TRIPLE;
        mram_read(from, cache, rem_size);
        mram_write(cache, out, rem_size);
        from += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    } while (from <= to);
}

/**
 * @brief Flushes the second run in a pair of runs once the tail of the first one is reached.
 * This includes only writing whatever is still in the cache to the MRAM.
 * 
 * @param ptr The current buffer item of the first second.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static void flush_second(T * const ptr, T __mram_ptr * const out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = *ptr;
    }
#endif
    mram_write(cache, out, i * sizeof(T));
}

/**
 * @brief Merges the `UNROLLING_CACHE_LENGTH` least items in the current pair of runs.
 * @internal If one of the runs does not contain sufficiently many items anymore,
 * bounds checks on both runs occur with each itemal merge. The reason is that
 * the unrolling everywhere makes the executable too big if the check is more fine-grained.
 * 
 * @param flush_0 An if block checking whether the tail of the first run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
 * @param flush_1  An if block checking whether the tail of the second run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
**/
#define UNROLLED_MERGE(flush_0, flush_1)                                          \
for (size_t j = 0; j < UNROLLING_CACHE_LENGTH / UNROLL_FACTOR; j++) {             \
    _Pragma("unroll")                                                             \
    for (size_t k = 0; k < UNROLL_FACTOR; k++) {                                  \
        if (val[0] <= val[1]) {                                                   \
            cache[i++] = val[0];                                                  \
            flush_0;                                                              \
            val[0] = *(ptr[0] = seqread_get_straight(ptr[0], sizeof(T), &sr[0])); \
        } else {                                                                  \
            cache[i++] = val[1];                                                  \
            flush_1;                                                              \
            val[1] = *(ptr[1] = seqread_get_straight(ptr[1], sizeof(T), &sr[1])); \
        }                                                                         \
    }                                                                             \
}

/**
 * @brief Merges the `UNROLLING_CACHE_LENGTH` least items in the current pair of runs and
 * writes them to the MRAM.
 * 
 * @param flush_0 An if block checking whether the tail of the first run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
 * @param flush_1  An if block checking whether the tail of the second run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
**/
#define MERGE_WITH_CACHE_FLUSH(flush_0, flush_1) \
UNROLLED_MERGE(flush_0, flush_1);                \
mram_write(cache, out, UNROLLING_CACHE_SIZE);    \
i = 0;                                           \
out += UNROLLING_CACHE_LENGTH;

/**
 * @brief Merges two MRAM runs. If the second run is depleted, the first one will not be flushed.
 * 
 * @param sr Two regular UPMEM readers on the two runs.
 * @param ptr The current buffer items of the runs.
 * @param ends The last items of the two runs.
 * @param out Whither the merged runs are written.
**/
static void merge_half_space(seqreader_t sr[2], T *ptr[2], T __mram_ptr * const ends[2],
        T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    size_t i = 0;
    T val[2] = { *ptr[0], *ptr[1] };
    if (*ends[0] <= *ends[1]) {
        T __mram_ptr * const early_end = ends[0] - UNROLLING_CACHE_LENGTH;
        while (seqread_tell_straight(ptr[0], &sr[0]) <= early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        if (seqread_tell_straight(ptr[0], &sr[0]) > ends[0]) {
            return;
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                if (seqread_tell_straight(ptr[0], &sr[0]) >= ends[0]) {
                    flush_second(ptr[1], out, i);
                    return;
                },
                {}
            );
        }
    } else {
        T __mram_ptr * const early_end = ends[1] - UNROLLING_CACHE_LENGTH;
        while (seqread_tell_straight(ptr[1], &sr[1]) <= early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        if (seqread_tell_straight(ptr[1], &sr[1]) > ends[1]) {
            // I have no idea why this flush is more performant than its `emptied` variant.
            flush_first(ptr[0], seqread_tell_straight(ptr[0], &sr[0]), ends[0], out, i);
            return;
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                {},
                if (seqread_tell_straight(ptr[1], &sr[1]) >= ends[1]) {
                    flush_first(ptr[0], seqread_tell_straight(ptr[0], &sr[0]), ends[0], out, i);
                    return;
                }
            );
        }
    }
}

/**
 * @brief An implementation of MergeSort that only uses `n`/2 additional space.
 * 
 * @param start The first item of the MRAM array to sort.
 * @param end The last item of said array.
**/
static void merge_sort_half_space(T __mram_ptr * const start, T __mram_ptr * const end) {
    /* Starting runs. */
    form_starting_runs(start, end);

    /* Merging. */
    seqreader_t sr[2];
    size_t const n = end - start + 1;
    T __mram_ptr * const out = (T __mram_ptr *)((uintptr_t)output + (uintptr_t)start);
    for (size_t run_length = STARTING_RUN_LENGTH; run_length < n; run_length *= 2) {
        for (
            T __mram_ptr *run_1_end = end - run_length, *run_2_end = end;
            (intptr_t)run_1_end >= (intptr_t)start;
            run_1_end -= 2 * run_length, run_2_end -= 2 * run_length
        ) {
            // Copy the current run …
            T __mram_ptr *run_1_start = ((intptr_t)(run_1_end - run_length + 1) > (intptr_t)start)
                    ? run_1_end - run_length + 1
                    : start;
            copy_run(run_1_start, run_1_end, out);
            // … and merge the copy with the next run.
            T __mram_ptr * const ends[2] = { out + (run_1_end - run_1_start), run_2_end };
            T *ptr[2] = {
                seqread_init_straight(buffers[me()].seq_1, out, &sr[0]),
                seqread_init_straight(buffers[me()].seq_2, run_1_end + 1, &sr[1]),
            };
            merge_half_space(sr, ptr, ends, run_1_start);
        }
    }
}

union algo_to_test __host algos[] = {
    {{ "MergeHSRegular", { .mram = merge_sort_half_space } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
    if (me() != 0) return EXIT_SUCCESS;

    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 0x1000;
        host_to_dpu.offset = DMA_ALIGNED(host_to_dpu.length * sizeof(T)) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }

    /* Perform test. */
    mram_range range = { 0, host_to_dpu.length };
    sort_algo_mram * const algo = algos[host_to_dpu.algo_index].data.fct.mram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());

        array_stats stats_before;
        get_stats_unsorted(input, cache, range, false, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        time new_time = perfcounter_get();
        algo(&input[range.start], &input[range.end - 1]);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
        dpu_to_host.firsts += new_time;
        dpu_to_host.seconds += new_time * new_time;

        array_stats stats_after;
        get_stats_sorted(input, cache, range, false, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        range.start += host_to_dpu.offset;
        range.end += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
