#include <stdio.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>

#include "../support/common.h"
#include "checkers.h"
#include "random.h"

__host dpu_arguments_t DPU_INPUT_ARGUMENTS;

static struct xorshift rngs[NR_TASKLETS];

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

BARRIER_INIT(wait_for_reset, NR_TASKLETS);

int main() {
    const thread_id_t tid = me();
        if (tid == 0) {
        mem_reset();
                // printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        // printf("input size: %d\n", DPU_INPUT_ARGUMENTS.size);
        // printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        // printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
    }
    barrier_wait(&wait_for_reset);

    // input length per DPU in number of elements
    uint32_t length = DPU_INPUT_ARGUMENTS.length;
    // input size per DPU in bytes (aligned on 8 bytes)
    uint32_t size = DPU_INPUT_ARGUMENTS.size;
    // address of the current processing block in MRAM
    uint32_t mram_base_addr_A = (uint32_t)DPU_MRAM_HEAP_POINTER;
    // offset of the tasklet's first processing block in MRAM
    uint32_t base_tasklet = tid << BLOCK_SIZE_LOG2;

    // Initialize a local cache to store the MRAM block.
    T *cache_A = (T *) mem_alloc(BLOCK_SIZE);

        rngs[tid] = seed_xs(tid + 0b100111010);  // The binary number is arbitrarily chosen to introduce some 1s to improve the seed.
    for (uint32_t byte_index = base_tasklet; byte_index < size; byte_index += BLOCK_SIZE * NR_TASKLETS) {
        // bound checking
        uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= size) ? (size - byte_index) : BLOCK_SIZE;

        for (uint32_t j = 0; j < l_size_bytes >> DIV; j++) {
            T r = rr((T)DPU_INPUT_ARGUMENTS.upper_bound, &rngs[tid]);
            cache_A[j] = r;
        }
        // print_array(cache_A, l_size_bytes >> DIV);
        mram_write(cache_A, (__mram_ptr void*)(mram_base_addr_A + byte_index), l_size_bytes);
    }
    
    if (tid == 0) {
                T *cache_B = (T *) mem_alloc(size);
        for (uint32_t byte_index = 0; byte_index < size; byte_index += BLOCK_SIZE) {
            uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= size) ? (size - byte_index) : BLOCK_SIZE;
            mram_read((__mram_ptr void*)(mram_base_addr_A + byte_index), (void *)(cache_B + (byte_index >> DIV)), l_size_bytes);
        }
                // print_array(cache_B, input_length);
        insertion_sort(cache_B, length);
                // print_array(cache_B, input_length);
        if (is_sorted(cache_B, length) && is_uniform(cache_B, length, DPU_INPUT_ARGUMENTS.upper_bound)) {
        printf("O.K.!\n");
        } else {
        printf("K.O...\n");
        }
    }
    return 0;
}