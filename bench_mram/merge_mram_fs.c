/**
 * @file
 * @brief Measuring runtimes of a full-space MergeSort (sequential, MRAM, regular readers).
**/

#include <stdlib.h>
#include <string.h>

#include <alloc.h>
#include <barrier.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "checkers.h"
#include "communication.h"
#include "random_distribution.h"
#include "random_generator.h"

#define MRAM_MERGE FULL_SPACE
#include "mram_merging.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM * 3 / 4];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM * 3 / 4];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

BARRIER_INIT(omni_barrier, NR_TASKLETS);

seqreader_t sr[NR_TASKLETS][2];  // sequential readers used to read runs
static bool flipped[NR_TASKLETS];  // Whether a write-back from the auxiliary array is (not) needed.

/**
 * @brief An implementation of MergeSort that only uses `n`/2 additional space.
 * 
 * @param start The first item of the MRAM array to sort.
 * @param end The last item of said array.
**/
static void merge_sort_full_space(T __mram_ptr * const start, T __mram_ptr * const end) {
    /* Starting runs. */
    form_starting_runs(start, end);

    /* Merging. */
    seqreader_buffer_t wram[2] = { buffers[me()].seq_1, buffers[me()].seq_2 };
    T __mram_ptr *in, *until, *out;  // Runs from `in` to `until` are merged and stored in front of `out`.
    bool flip = false;  // Used to determine the initial positions of `in`, `out`, and `until`.
    size_t const n = end - start + 1;
    for (size_t run_length = STARTING_RUN_LENGTH; run_length < n; run_length *= 2) {
        // Set the positions to read from and write to.
        if ((flip = !flip)) {
            in = start;
            until = end;
            out = (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output) + n;
        } else {
            in = (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output);
            until = (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output) + n - 1;
            out = end + 1;
        }
        // Merge pairs of neighboured runs.
        T __mram_ptr *run_1_end = until - run_length;
        for (; (intptr_t)run_1_end >= (intptr_t)in; run_1_end -= 2*run_length) {
            T __mram_ptr *run_1_start;
            if ((intptr_t)(run_1_end + 1 - run_length) >= (intptr_t)in) {
                run_1_start = run_1_end + 1 - run_length;
                out -= 2*run_length;
            } else {
                run_1_start = in;
                out -= run_length + (run_1_end - run_1_start + 1);
            }
            T __mram_ptr * const ends[2] = { run_1_end, run_1_end + run_length };
            T *ptr[2] = {
                sr_init(buffers[me()].seq_1, run_1_start, &sr[me()][0]),
                sr_init(buffers[me()].seq_2, run_1_end + 1, &sr[me()][1]),
            };
            merge(ptr, ends, out, wram);
        }
        // Flush single run at the beginning straight away
        if ((intptr_t)(run_1_end + run_length) >= (intptr_t)in) {
            out = (flip) ? (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output) : start;
            copy_run(in, run_1_end + run_length, out);
        }
    }
    flipped[me()] = flip;
}

union algo_to_test __host algos[] = {
    {{ "MergeFS", { .mram = merge_sort_full_space } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
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
        host_to_dpu.part_length = ALIGN(
            DIV_CEIL(host_to_dpu.length, NR_TASKLETS) * sizeof(T),
            16
        ) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }

    /* Perform test. */
    mram_range range = {
        me() * host_to_dpu.part_length,
        (me() == NR_TASKLETS - 1) ? host_to_dpu.offset : (me() + 1) * host_to_dpu.part_length,
    };
    sort_algo_mram * const algo = algos[host_to_dpu.algo_index].data.fct.mram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());

        array_stats stats_before;
        get_stats_unsorted(input, cache, range, false, &stats_before);

        barrier_wait(&omni_barrier);
        perfcounter_config(COUNT_CYCLES, true);
        time new_time;
        if (me() == 0)
            new_time = perfcounter_get();
        algo(&input[range.start], &input[range.end - 1]);
        if (me() == 0) {
            new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
            dpu_to_host.firsts += new_time;
            dpu_to_host.seconds += new_time * new_time;
        }
        barrier_wait(&omni_barrier);

        array_stats stats_after;
        T __mram_ptr *sorted_array = (flipped[me()]) ? output : input;
        flipped[me()] = false;  // Following sorting algorithms may not reset this value.
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
