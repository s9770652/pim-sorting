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

#define LOAD_INTO_WRAM ((1024 * 50) >> DIV)  // maximum number of elements loaded into WRAM
#define LOAD_INTO_MRAM ((1024 * 1024 * 50) >> DIV)
#define PERF 1
#define CHECK_ORDER 1

__host dpu_arguments_t DPU_INPUT_ARGUMENTS;
T __mram_noinit elements[LOAD_INTO_MRAM];
static struct xorshift rngs[NR_TASKLETS];  // Contains the seeds for each tasklet.
#if PERF
perfcounter_t cycles[NR_TASKLETS];  // Used to measure the time for each tasklet.
#endif
#if CHECK_ORDER
bool sorted = true;
MUTEX_INIT(sorting_mutex);
#endif

void insertion_sort(T arr[], int len) {
    for (int i = 1; i < len; i++) {
        T to_sort = arr[i];
        int j = i;
        while ((j > 0) && (arr[j-1] > to_sort)) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = to_sort;
    }
}

BARRIER_INIT(omni_barrier, NR_TASKLETS);

int main() {
    const thread_id_t tid = me();
    if (tid == 0) {
        mem_reset();
#if PERF
        perfcounter_config(COUNT_CYCLES, true);
#endif
        printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        printf("input size: %d\n", DPU_INPUT_ARGUMENTS.size);
        printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
        printf("T in MRAM: %d\n", LOAD_INTO_MRAM);
        printf("free in MRAM: %d\n", 1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER);
        printf("more T in MRAM: %d\n", (1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER) >> DIV);
    }
    barrier_wait(&omni_barrier);

    /* Compute addresses and boundaries of arrays in WRAM and MRAM. */
    // input length per DPU in number of elements
    const uint32_t length = DPU_INPUT_ARGUMENTS.length;
    // input length such that the size is aligned on 8 bytes
    const uint32_t length_aligned = ROUND_UP_POW2(length << DIV, 8) >> DIV;
    // maxmium length of each block
    const uint32_t block_length = BLOCK_SIZE >> DIV;
    // // start of the tasklet's first processing block in MRAM
    const uint32_t offset = (tid * block_length) >> DIV;

    /* Write random numbers onto the MRAM. */
    rngs[tid] = seed_xs(tid + 0b100111010);  // The binary number is arbitrarily chosen to introduce some 1s to improve the seed.
#if PERF
    cycles[tid] = perfcounter_get();
#endif
    // Initialize a local cache to store one MRAM block.
    __dma_aligned T *cache = (T *) mem_alloc(BLOCK_SIZE);
    uint32_t curr_size = BLOCK_SIZE;
    for (uint32_t i = offset; i < length; i += block_length * NR_TASKLETS) {
        if (i + block_length > length_aligned) {
            curr_size = (length_aligned - i) << DIV;
        }
        for (uint32_t j = 0; j < curr_size >> DIV; j++) {
            // cache[j] = rr((T)DPU_INPUT_ARGUMENTS.upper_bound, &rngs[tid]);
            cache[j] = i + j;
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
    

    /* Check if numbers were correctly sorted. */
#if CHECK_ORDER
#if PERF
    cycles[tid] = perfcounter_get();
#endif
    curr_size = BLOCK_SIZE;
    for (uint32_t i = offset; i < length; i += block_length * NR_TASKLETS) {
        if (!sorted) break;
        if (i + block_length > length_aligned) {
            curr_size = (length_aligned - i) << DIV;
        }
        mram_read(&elements[i], cache, curr_size);
        if (!is_sorted(cache, curr_size >> DIV) || (i > 0 && elements[i-1] > cache[0])) {
            mutex_lock(sorting_mutex);
            sorted = false;
            mutex_unlock(sorting_mutex);
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