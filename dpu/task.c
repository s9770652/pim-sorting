#include <stdlib.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

#include "common.h"
#include "communication.h"
#include "buffers.h"
#include "mram_loop.h"
#include "sort.h"
#include "random_distribution.h"

#if PERF
#include "timer.h"
#endif
#if CHECK_SANITY
#include "checkers.h"
#endif

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
T __mram_noinit input[LOAD_INTO_MRAM];  // array of random numbers
T __mram_noinit output[LOAD_INTO_MRAM];
mram_range ranges[NR_TASKLETS];
struct xorshift rngs[NR_TASKLETS];
bool flipped;  // Whether `input` or `output` contains the final sorted sequence.
bool dummy;  // Whether a dummy value was set at the end of the input sequence.

BARRIER_INIT(omni_barrier, NR_TASKLETS);

#if PERF
perfcounter_t cycles[NR_TASKLETS];  // Used to measure the time for each tasklet.
#endif


inline size_t align(size_t to_align) {
    return ROUND_UP_POW2(to_align << DIV, 8) >> DIV;
}

int main(void) {
    if (me() == 0) {
        mem_reset();
        dummy = false;
#if PERF
        perfcounter_config(COUNT_CYCLES, true);
#endif
#include <stdio.h>
        printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        printf("diff to max length: %d\n", LOAD_INTO_MRAM - DPU_INPUT_ARGUMENTS.length);
        printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        // printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
        // printf("T in MRAM: %d\n", 2 * LOAD_INTO_MRAM);
        // printf("free in MRAM: %d\n", 1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER);
        // printf("more T in MRAM: %d\n", (1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER) >> DIV);
    }
    barrier_wait(&omni_barrier);

    /* Compute addresses and boundaries of arrays in WRAM and MRAM. */
    // input length per DPU in number of elements
    const size_t length = DPU_INPUT_ARGUMENTS.length ?: 0x600;
    // maximum number of elements in the subarray filled by each tasklet
    const size_t part_length = align(DIV_CEIL(length, NR_TASKLETS));
    // start of the tasklet’s subarray
    const size_t part_start = me() * part_length;
    // end of the tasklet’s subarray
#ifdef UINT32
    const size_t part_end = (me() == NR_TASKLETS - 1) ? ROUND_UP_POW2(length, 2) : part_start + part_length;
#else
    const size_t part_end = (me() == NR_TASKLETS - 1) ? length : part_start + part_length;
#endif
    // Setting the ranges of MRAM indices on which each tasklet operates.
    // Note: For the last tasklet this may or may not include a dummy variable (see below).
    ranges[me()].start = part_start;
    ranges[me()].end = part_end;

    /* Write random elements onto the MRAM. */
    rngs[me()] = seed_xs(me() + 0b1011100111010);
    triple_buffers buffers;
    allocate_triple_buffer(&buffers);
    T *cache = buffers.cache;

#if PERF
    cycles[me()] = perfcounter_get();
#endif

    generate_uniform_distribution_mram(input, cache, &ranges[me()], 0);
#ifdef UINT32
    // Add a dummy variable such that the last initial run has a length disible by 8.
    // This way, depleting (cf. `sort.c`) need less meddling with unaligned addresses.
    if (me() == NR_TASKLETS - 1 && length & 1) {
        input[length] = UINT32_MAX;
        dummy = true;
    }
#endif

#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
    barrier_wait(&omni_barrier);
    print_time(cycles, "MEMORY");
#endif
    barrier_wait(&omni_barrier);

    /* Gather info about generated elements for validation of the sorted ones. */
#if CHECK_SANITY
    print_array(input, cache, length, "Before sorting");
#if PERF
    barrier_wait(&omni_barrier);
    cycles[me()] = perfcounter_get();
#endif

    array_stats stats_1;
    get_stats_unsorted(input, cache, ranges[me()], dummy, &stats_1);

#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
    barrier_wait(&omni_barrier);
    print_time(cycles, "CHECK1");
#endif
    barrier_wait(&omni_barrier);
#endif

    /* Sort the elements. */
#if PERF
    cycles[me()] = perfcounter_get();
#endif

    bool flipped_own = sort(input, output, &buffers, ranges);

#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
    barrier_wait(&omni_barrier);
    print_time(cycles, "SORT");
#endif

    if (me() == 0) flipped = flipped_own;
    barrier_wait(&omni_barrier);
    T __mram_ptr *within = (flipped) ? output : input;
    (void)within;  // Surpresses warning if no sanity checks are done.

    /* Validate sorted elements. */
#if CHECK_SANITY
    print_array(within, cache, length, "After sorting");
#if PERF
    barrier_wait(&omni_barrier);
    cycles[me()] = perfcounter_get();
#endif

    array_stats stats_2;
    get_stats_sorted(within, cache, ranges[me()], dummy, &stats_2);

#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
    barrier_wait(&omni_barrier);
    print_time(cycles, "CHECK2");
#endif
    compare_stats(&stats_1, &stats_2);
#endif
    return EXIT_SUCCESS;
}
