/**
 * @file
 * @brief Measures runtimes of MergeSorts (sequential, MRAM).
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

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

#define STARTING_RUN_LENGTH (TRIPLE_BUFFER_LENGTH)
#define STARTING_RUN_SIZE (STARTING_RUN_LENGTH << DIV)
static_assert(
    STARTING_RUN_SIZE == DMA_ALIGNED(STARTING_RUN_SIZE),
    "The size of starting runs must be properly aligned for DMAs!"
);
static_assert(
    STARTING_RUN_SIZE <= TRIPLE_BUFFER_SIZE,
    "The starting runs are sorted entirely in WRAM and, thus, must fit in there!"
);

static void form_starting_runs_half_space(T __mram_ptr * const start, T __mram_ptr * const end) {
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

static void flush_starting_run(T __mram_ptr *in, T __mram_ptr *until, T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    mram_range_ptr range = { in, until + 1 };
    T __mram_ptr *i;
    size_t curr_length, curr_size;
    LOOP_ON_MRAM_BL(i, curr_length, curr_size, range, MAX_TRANSFER_LENGTH_TRIPLE) {
        mram_read_triple(i, cache, curr_size);
        mram_write_triple(cache, out, curr_size);
        out += curr_length;
    }
}

static void flush_first(T __mram_ptr *in, T __mram_ptr *out, __attribute__((unused)) T * const ptr,
        size_t i, T __mram_ptr const *end) {
    T * const cache = buffers[me()].cache;
    // Transfer cache to MRAM.
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        cache[i++] = *ptr;  // Possible since `ptr` must have at least one element.
        if (in >= end) {
            mram_write(cache, out, i * sizeof(T));
            return;
        }
        in++;
    }
#endif
    mram_write(cache, out, i * sizeof(T));
    out += i;

    // Transfer from MRAM to MRAM.
    do {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        size_t const rem_size = (in + MAX_TRANSFER_LENGTH_TRIPLE > end)
                ? (size_t)end - (size_t)in + sizeof(T)
                : MAX_TRANSFER_SIZE_TRIPLE;
        mram_read(in, cache, rem_size);
        mram_write(cache, out, rem_size);
        in += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    } while (in <= end);
}

static void flush_second(T __mram_ptr * const out, __attribute__((unused)) T * const ptr,
        size_t i) {
    T * const cache = buffers[me()].cache;
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        cache[i++] = *ptr;  // Possible since `ptr` must have at least one element.
    }
#endif
    mram_write(cache, out, i * sizeof(T));
}

static void merge_half_space(T __mram_ptr *out, T __mram_ptr * const ends[2], seqreader_t sr[2],
        T *ptr[2], size_t elems_left[2]) {
    T * const cache = buffers[me()].cache;
    size_t i = 0;
    while (true) {
        if (*ptr[0] <= *ptr[1]) {
            cache[i++] = *ptr[0];
            ptr[0] = seqread_get(ptr[0], sizeof(T), &sr[0]);
            if (--elems_left[0] == 0) {
                flush_second(out, ptr[1], i);
                break;
            }
        } else {
            cache[i++] = *ptr[1];
            ptr[1] = seqread_get(ptr[1], sizeof(T), &sr[1]);
            if (--elems_left[1] == 0) {
                flush_first(seqread_tell(ptr[0], &sr[0]), out, ptr[0], i, ends[0]);
                break;
            }
        }
        // If the cache is full, write its content to `output`.
        if (i == MAX_TRANSFER_LENGTH_CACHE) {
            mram_write(cache, out, MAX_TRANSFER_SIZE_CACHE);
            i = 0;
            out += MAX_TRANSFER_LENGTH_CACHE;
        }
    }
}

static void merge_sort_half_space(T __mram_ptr * const start, T __mram_ptr * const end) {
    /* Starting runs. */
    form_starting_runs_half_space(start, end);

    /* Merging. */
    seqreader_t sr[2];
    size_t const n = end - start + 1;
    for (size_t run_length = STARTING_RUN_LENGTH; run_length < n; run_length *= 2) {
        // Merge pairs of adjacent runs.
        for (T __mram_ptr *i = end; i > start; i -= 2 * run_length) {
            // Only one run left?
            T __mram_ptr * const run_1_end = i - run_length;
            if ((intptr_t)run_1_end < (intptr_t)start) {
                break;
            }
            // If not, copy the current run …
            T __mram_ptr *run_1_start = ((intptr_t)(run_1_end - run_length + 1) > (intptr_t)start)
                    ? run_1_end - run_length + 1
                    : start;
            flush_starting_run(run_1_start, run_1_end, output);
            // … and merge the copy with the next run.
            T __mram_ptr * const ends[2] = { output + (run_1_end - run_1_start), i };
            T *ptr[2] = {
                seqread_init(buffers[me()].seq_1, output, &sr[0]),
                seqread_init(buffers[me()].seq_2, run_1_end + 1, &sr[1]),
            };
            size_t elems_left[2] = { run_1_end - run_1_start + 1, run_length };
            merge_half_space(
                run_1_start,
                ends,
                sr,
                ptr,
                elems_left
            );
        }
    }
}

union algo_to_test __host algos[] = {
    {{ "MergeHalfSpace", { .mram = merge_sort_half_space } }},
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
        host_to_dpu.length = 1024;
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
