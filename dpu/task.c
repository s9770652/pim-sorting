#include <stddef.h>
#include <stdio.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <perfcounter.h>

#include "../support/common.h"
#include "checkers.h"
#include "sort.h"
#include "random.h"

// maximum number of elements loaded into MRAM
#define LOAD_INTO_MRAM ((1024 * 1024 * 25) >> DIV)
#define PERF 1
#define CHECK_SANITY 1

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
T __mram_noinit input[LOAD_INTO_MRAM];  // array of random numbers
T __mram_noinit output[LOAD_INTO_MRAM];
static struct xorshift rngs[NR_TASKLETS];  // Contains the seed for each tasklet.
#if PERF
perfcounter_t cycles[NR_TASKLETS];  // Used to measure the time for each tasklet.
#endif
#if CHECK_SANITY
bool sorted;
MUTEX_INIT(sorted_mutex);
#endif

BARRIER_INIT(omni_barrier, NR_TASKLETS);

inline size_t align(size_t to_align) {
    return ROUND_UP_POW2(to_align << DIV, 8) >> DIV;
}

int main() {
    if (me() == 0) {
        mem_reset();
#if PERF
        perfcounter_config(COUNT_CYCLES, true);
#endif
#if CHECK_SANITY
        sorted = true;
#endif
        printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        // printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
        // printf("T in MRAM: %d\n", 2 * LOAD_INTO_MRAM);
        // printf("free in MRAM: %d\n", 1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER);
        // printf("more T in MRAM: %d\n", (1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER) >> DIV);
    }
    barrier_wait(&omni_barrier);

    /* Compute addresses and boundaries of arrays in WRAM and MRAM. */
    // input length per DPU in number of elements
    const size_t length = DPU_INPUT_ARGUMENTS.length;
    // maximum number of elements in the subarray filled by each tasklet
    const size_t part_length = align(DIV_CEIL(length, NR_TASKLETS));
    // start of the tasklet's subarray
    const size_t part_start = me() * part_length;
    // end of the tasklet's subarray
    const size_t part_end = (me() == NR_TASKLETS - 1) ? length : part_start + part_length;
    // end of the tasklet's subarray, aligned on 8 bytes
    const size_t part_end_aligned = (me() == NR_TASKLETS - 1) ? align(part_end) : part_end;
    const struct mram_range range = { part_start, part_end, part_end_aligned };

    /* Write random numbers onto the MRAM. */
    rngs[me()] = seed_xs(me() + 0b100111010);  // The binary number is arbitrarily chosen to introduce some 1s to improve the seed.
    // Initialize a local cache to store one MRAM block.
    T *cache = mem_alloc(3 * BLOCK_SIZE);
#if PERF
    cycles[me()] = perfcounter_get();
#endif
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        for (size_t j = 0; j < curr_length; j++) {
            // cache[j] = rr((T)DPU_INPUT_ARGUMENTS.upper_bound, &rngs[me()]);
            cache[j] = (gen_xs(&rngs[me()]) & 7);
            // cache[j] = i + j;
            // cache[j] = length - (i + j) - 1;
            // cache[j] = 0;
        }
        mram_write(cache, &input[i], curr_size);
    }
#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
#endif
    barrier_wait(&omni_barrier);
#if PERF
    if (me() == 0) {
        get_time(cycles, "MEMORY");
    }
    barrier_wait(&omni_barrier);
#endif

#if CHECK_SANITY
    uint64_t sum = 0;
    size_t cnts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (me() == 0) {
        printf("Before sorting:\n");
        mram_range testrange = { 0, length, length };
        LOOP_ON_MRAM(i, curr_length, curr_size, testrange) {
            mram_read(&input[i], cache, curr_size);
            // print_array(cache, curr_length);
            for (size_t x = 0; x < curr_length; x++) {
                sum += cache[x];
                if (cache[x] < 8)
                    cnts[cache[x]]++;
            }
        }
        printf("\n");
    }
    barrier_wait(&omni_barrier);
#endif

    /* Sort the elements. */
#if PERF
    cycles[me()] = perfcounter_get();
#endif
    int flipped = sort(input, output, cache, range);
#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
#endif
    barrier_wait(&omni_barrier);
    T __mram_ptr *within = (flipped) ? output : input;
#if PERF
    if (me() == 0) {
        get_time(cycles, "SORT");
    }
    barrier_wait(&omni_barrier);
#endif

#if CHECK_SANITY
    uint64_t sumo = 0;
    size_t cntso[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (me() == 0) {
        printf("After sorting:\n");
        mram_range testrange = { 0, length, length };
        LOOP_ON_MRAM(i, curr_length, curr_size, testrange) {
            mram_read(&within[i], cache, curr_size);
            // print_array(cache, curr_length);
            for (size_t x = 0; x < curr_length; x++) {
                sumo += cache[x];
                if (cache[x] < 8)
                    cntso[cache[x]]++;
            }
        }
        printf("sums: %lu %lu\n", sum, sumo);
        printf("counts: ");
        for (size_t c = 0; c < 8; c++)
            printf("%d: %zu %zu  ", c, cnts[c], cntso[c]);
        printf("\n");
    }
    barrier_wait(&omni_barrier);
#endif

    /* Check if numbers were correctly sorted. */
#if CHECK_SANITY
#if PERF
    cycles[me()] = perfcounter_get();
#endif
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        if (!sorted) break;
        mram_read(&within[i], cache, curr_size);
        if (!is_sorted(cache, curr_length) || (i > 0 && within[i-1] > cache[0])) {
            mutex_lock(sorted_mutex);
            sorted = false;
            mutex_unlock(sorted_mutex);
            break;
        }
    }
#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
#endif
    barrier_wait(&omni_barrier);
#if PERF
    if (me() == 0) {
        get_time(cycles, "CHECK");
    }
    barrier_wait(&omni_barrier);
#endif
    if (me() == 0) {
        if (sorted) {
            printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Elements are sorted.\n");
        } else {
            printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements are not sorted.\n");
        }
    }
#endif
    return 0;
}