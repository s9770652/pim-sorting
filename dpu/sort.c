#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <alloc.h>
#include <defs.h>
#include <mram.h>

#include "../support/common.h"
#include "checkers.h"
#include "sort.h"

#define INSERTION_SIZE 2048

void insertion_sort(T arr[], size_t len) {
    for (size_t i = 1; i < len; i++) {
        T to_sort = arr[i];
        size_t j = i;
        while ((j > 0) && (arr[j-1] > to_sort)) {
            arr[j] = arr[j-1];
            j--;
        }
        arr[j] = to_sort;
    }
}

void sort(T __mram_ptr *elements, T *cache_in1, const size_t offset, const size_t length, const size_t length_aligned, const size_t block_length) {
    /* Insertion sort by each tasklet. */
    size_t curr_size = BLOCK_SIZE, curr_length = block_length;
    for (size_t i = offset; i < length; i += block_length * NR_TASKLETS) {
        if (i + block_length > length_aligned) {
            curr_length = length_aligned - i;
            curr_size = curr_length << DIV;
        }
        mram_read(&elements[i], cache_in1, curr_size);
        insertion_sort(cache_in1, curr_length);
        mram_write(cache_in1, &elements[i], curr_size);
    }
    /* Merge by tasklets. */
    T *cache_in2 = mem_alloc(BLOCK_SIZE);
    T *cache_out = mem_alloc(BLOCK_SIZE);
    const size_t max_run_length = DIV_CEIL(length_aligned, NR_TASKLETS);
    const size_t run_offset = me() * max_run_length;
    size_t next_run_offset = (run_offset + max_run_length > length) ? length : run_offset + max_run_length;
    curr_length = block_length << 1;
    for (size_t i = run_offset; i < next_run_offset; i += block_length << 1) {
        if (i + block_length > next_run_offset) {
            curr_length = next_run_offset - i;
            curr_size = curr_length << DIV;
        }
        if (curr_size > BLOCK_SIZE) {
            mram_read(&elements[i], cache_in1, BLOCK_SIZE);
            mram_read(&elements[i+block_length], cache_in2, curr_size - BLOCK_SIZE);
        }
    }
}