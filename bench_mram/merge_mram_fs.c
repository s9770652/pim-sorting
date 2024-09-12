/**
 * @file
 * @brief Measuring runtimes of a full-space MergeSort (sequential, MRAM, regular readers).
**/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <barrier.h>
#include <defs.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "checkers.h"
#include "communication.h"
#include "mram_sorts.h"
#include "random_distribution.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot
array_stats stats_before, stats_after;
time times[NR_TASKLETS];

BARRIER_INIT(omni_barrier, NR_TASKLETS);

seqreader_t sr[NR_TASKLETS][2];  // sequential readers used to read runs
bool flipped[NR_TASKLETS];  // Whether `output` contains the latest sorted runs.

static void merge_sort_full_space(T __mram_ptr * const start, T __mram_ptr * const end) {
    merge_sort_mram(start, end);
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
    if (me() == 0 && host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 0x1000;
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
    sort_algo_mram * const algo = algos[host_to_dpu.algo_index].data.fct.mram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());

        get_stats_unsorted(input, cache, range, false, &stats_before);

        barrier_wait(&omni_barrier);
        perfcounter_config(COUNT_CYCLES, true);
        time new_time = perfcounter_get();
        algo(&input[range.start], &input[range.end - 1]);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
        times[me()] = new_time;
        barrier_wait(&omni_barrier);
        if (me() == 0) {
            for (size_t i = 1; i < NR_TASKLETS; i++)
                times[0] += times[i];
            dpu_to_host.firsts += times[0];
            dpu_to_host.seconds += times[0] * times[0];
        }

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
