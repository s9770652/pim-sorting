#include <stdio.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

#include "../support/common.h"
#include "checkers.h"
#include "random.h"

__host dpu_arguments_t DPU_INPUT_ARGUMENTS;
perfcounter_t cycles[NR_TASKLETS];  // Used to measure the time for each tasklet.
static struct xorshift rngs[NR_TASKLETS];  // Contains the seeds for each tasklet.

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
        perfcounter_config(COUNT_CYCLES, true);
        printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        printf("input size: %d\n", DPU_INPUT_ARGUMENTS.size);
        printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
        printf("free in MRAM: %d\n", 1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER);
        printf("max T in MRAM: %d\n", (1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER) >> DIV);
    }
    barrier_wait(&omni_barrier);

    /* Compute addresses and boundaries of arrays in WRAM and MRAM. */
    // input length per DPU in number of elements
    uint32_t length = DPU_INPUT_ARGUMENTS.length;
    // input size per DPU in bytes (aligned on 8 bytes)
    uint32_t size = DPU_INPUT_ARGUMENTS.size;
    // address of the current processing block in MRAM
    uint32_t mram_base_addr_A = (uint32_t)DPU_MRAM_HEAP_POINTER;
    // offset of the tasklet's first processing block in MRAM
    uint32_t base_tasklet = tid << BLOCK_SIZE_LOG2;

    /* Write random numbers into the MRAM. */
    rngs[tid] = seed_xs(tid + 0b100111010);  // The binary number is arbitrarily chosen to introduce some 1s to improve the seed.
    cycles[tid] = perfcounter_get();
    T *cache = (T *) mem_alloc(BLOCK_SIZE);  // Initialize a local cache to store the MRAM block.
    uint32_t byte_index, l_size_bytes;
    LOOP_ON_MRAM(byte_index, base_tasklet, size, BLOCK_SIZE, NR_TASKLETS, l_size_bytes) {
        for (uint32_t j = 0; j < l_size_bytes >> DIV; j++) {
            cache[j] = rr((T)DPU_INPUT_ARGUMENTS.upper_bound, &rngs[tid]);
        }
        mram_write(cache, (__mram_ptr void *)(mram_base_addr_A + byte_index), l_size_bytes);
    }
    cycles[tid] = perfcounter_get() - cycles[tid];
    barrier_wait(&omni_barrier);
    if (tid == 0) {
        get_time(cycles, "MEMORY");
    }
    barrier_wait(&omni_barrier);

    if (tid == 0) {
        T *cache_B = (T *) mem_alloc(size);
        for (uint32_t byte_index = 0; byte_index < size; byte_index += BLOCK_SIZE) {
            uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= size) ? (size - byte_index) : BLOCK_SIZE;
            mram_read((__mram_ptr void *)(mram_base_addr_A + byte_index), (void *)(cache_B + (byte_index >> DIV)), l_size_bytes);
        }
        // print_array(cache_B, length);
        insertion_sort(cache_B, length);
        // print_array(cache_B, length);
        // if (is_sorted(cache_B, length) && is_uniform(cache_B, length, DPU_INPUT_ARGUMENTS.upper_bound)) {
        //     printf("O.K.!\n");
        // } else {
            //     printf("K.O...\n");
        // }
    }
    return 0;
}