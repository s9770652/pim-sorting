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
#include "random.h"

// maximum number of elements loaded into MRAM
#define LOAD_INTO_MRAM ((1024 * 1024 * 50) >> DIV)
#define PERF 1
#define CHECK_ORDER 1

__host dpu_arguments_t DPU_INPUT_ARGUMENTS;
T __mram_noinit elements[LOAD_INTO_MRAM];  // array of random numbers
static struct xorshift rngs[NR_TASKLETS];  // Contains the seed for each tasklet.
#if PERF
perfcounter_t cycles[NR_TASKLETS];  // Used to measure the time for each tasklet.
#endif
#if CHECK_ORDER
bool sorted = true;
MUTEX_INIT(sorted_mutex);
#endif

BARRIER_INIT(omni_barrier, NR_TASKLETS);

inline size_t align(size_t to_align) {
    return ROUND_UP_POW2(to_align << DIV, 8) >> DIV;
}

int main() {
    const thread_id_t tid = me();
    if (tid == 0) {
        mem_reset();
#if PERF
        perfcounter_config(COUNT_CYCLES, true);
#endif
        // printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        // printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        // printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
        // printf("T in MRAM: %d\n", LOAD_INTO_MRAM);
        // printf("free in MRAM: %d\n", 1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER);
        // printf("more T in MRAM: %d\n", (1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER) >> DIV);
    }
    barrier_wait(&omni_barrier);

    /* Compute addresses and boundaries of arrays in WRAM and MRAM. */
    // input length per DPU in number of elements
    const size_t length = DPU_INPUT_ARGUMENTS.length;
    // input length such that the size is aligned on 8 bytes
    const size_t length_aligned = align(length);
    // maxmium length of each block
    const size_t block_length = BLOCK_SIZE >> DIV;
    // maximum number of elements in the subarray filled by each tasklet
    const size_t part_length = align(DIV_CEIL(length, NR_TASKLETS));
    // start of the tasklet's subarray
    const size_t part_start = tid * part_length;
    // end of the tasklet's subarray
    const size_t part_end = (tid == NR_TASKLETS - 1) ? length : part_start + part_length;
    const size_t part_end_aligned = align(part_end);

    /* Write random numbers onto the MRAM. */
    rngs[tid] = seed_xs(tid + 0b100111010);  // The binary number is arbitrarily chosen to introduce some 1s to improve the seed.
#if PERF
    cycles[tid] = perfcounter_get();
#endif
    // Initialize a local cache to store one MRAM block.
    T *cache = mem_alloc(BLOCK_SIZE);  // todo: cast needed?
    size_t curr_length = block_length;  // number of elements read at once
    size_t curr_size = BLOCK_SIZE;  // size of elements read at once
    for (size_t i = part_start; i < part_end; i += block_length) {
        if (i + block_length > part_end) {
            curr_length = part_end - i;
            curr_size = (part_end_aligned - i) << DIV;
        }
        for (size_t j = 0; j < curr_length; j++) {
            cache[j] = rr((T)DPU_INPUT_ARGUMENTS.upper_bound, &rngs[tid]);
            // cache[j] = i + j;
            // cache[j] = length - (i + j);
            // cache[j] = 0;
        }
        mram_write(cache, &elements[i], curr_size);
    }
#if PERF
    cycles[tid] = perfcounter_get() - cycles[tid];
#endif
    barrier_wait(&omni_barrier);
#if PERF
    if (tid == 0) {
        get_time(cycles, "MEMORY");
    }
    barrier_wait(&omni_barrier);
#endif

    /* Sort the elements. */

    // if (tid == 0) {
    //     curr_size = BLOCK_SIZE;
    //     T *testcache = mem_alloc(length_aligned << DIV);
    //     for (uint32_t i = 0; i < length; i += block_length) {
    //         if (i + block_length > length_aligned) {
    //             curr_size = (length_aligned - i) << DIV;
    //         }
    //         mram_read(&elements[i], &testcache[i], curr_size);
    //     }
    //     print_array(testcache, length);
    // }
    // barrier_wait(&omni_barrier);

    /* Check if numbers were correctly sorted. */
#if CHECK_ORDER
#if PERF
    cycles[tid] = perfcounter_get();
#endif
    curr_size = BLOCK_SIZE;
    curr_length = block_length;
    for (size_t i = part_start; i < part_end; i += block_length) {
        if (!sorted) break;
        if (i + block_length > part_end) {
            curr_length = part_end - i;
            curr_size = (part_end_aligned - i) << DIV;
        }
        mram_read(&elements[i], cache, curr_size);
        if (!is_sorted(cache, curr_length) || (i > 0 && elements[i-1] > cache[0])) {
            mutex_lock(sorted_mutex);
            sorted = false;
            mutex_unlock(sorted_mutex);
            break;
        }
    }
#if PERF
    cycles[tid] = perfcounter_get() - cycles[tid];
#endif
    barrier_wait(&omni_barrier);
#if PERF
    if (tid == 0) {
        get_time(cycles, "CHECK");
    }
    barrier_wait(&omni_barrier);
#endif
    if (tid == 0) {
        if (sorted) {
            printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Elements are sorted.\n");
        } else {
            printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements are not sorted.\n");
        }
    }
#endif
    return 0;
}