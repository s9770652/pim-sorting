#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <seqread.h>

#include "../support/common.h"
#include "checkers.h"
#include "sort.h"

#define INSERTION_SIZE SEQREAD_CACHE_SIZE

BARRIER_INIT(sort_barrier, NR_TASKLETS);

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

bool merge(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range range) {
    seqreader_buffer_t buffers[2] = { seqread_alloc(), seqread_alloc() };
    seqreader_t sr[2];
    bool flipped = false;
    for (uint32_t run = BLOCK_LENGTH; run < range.end - range.start; run <<= 1) {
        for (uint32_t j = range.start; j < range.end; j += run << 1) {
            T *ptr[2] = {
                seqread_init(buffers[0], &input[j], &sr[0]),
                seqread_init(buffers[1], &input[j + run], &sr[1])
            };
            const T __mram_ptr *ends[2] = { &input[j + run], &input[j + run + run] };
            bool active = 0;
            size_t ptr_out = 0, filled_out = 0;
            while (1) {
                if (*ptr[!active] < *ptr[active]) {
                    active = !active;
                }
                cache[ptr_out++] = *ptr[active];
                ptr[active] = seqread_get(ptr[active], sizeof(T), &sr[active]);
                if (seqread_tell(ptr[active], &sr[active]) == ends[active]) {
                    // Fill `cache_out` up so that both it and the rest of `cache_in2` have a size aligned on 8 bytes.
                    // todo: fill up only as little as possible? Worth the extra checks/calculations? Though might be more costly due to more mram_writes!
                    for (uint32_t rest = ptr_out; rest < BLOCK_LENGTH; rest++) {
                        cache[rest] = *ptr[!active];
                        ptr[!active] = seqread_get(ptr[!active], sizeof(T), &sr[!active]);
                    }
                    // Empty `cache_out`.
                    mram_write(cache, &output[j + filled_out], BLOCK_SIZE);
                    filled_out += BLOCK_LENGTH;
                    // Finish reading `ptr[!active]`.
                    ptr_out = 0;
                    while (seqread_tell(ptr[!active], &sr[!active]) != ends[!active]) {
                        cache[ptr_out++] = *ptr[!active];
                        ptr[!active] = seqread_get(ptr[!active], sizeof(T), &sr[!active]);
                        if (ptr_out == BLOCK_LENGTH) {
                            mram_write(cache, &output[j + filled_out], BLOCK_SIZE);
                            filled_out += BLOCK_LENGTH;
                            ptr_out = 0;
                        }
                    }
                    if (filled_out != (run << 1)) {
                        mram_write(cache, &output[j + filled_out], (BLOCK_LENGTH - ptr_out) << DIV);
                    }
                    break;
                }
                if (ptr_out == BLOCK_LENGTH) {
                    mram_write(cache, &output[j + filled_out], BLOCK_SIZE);
                    ptr_out = 0;
                    filled_out += BLOCK_LENGTH;
                }
            }
        }
        T __mram_ptr *tmp = input;
        input = output;
        output = tmp;
        flipped = !flipped;
    }
    return flipped;
}

// todo: Adher to INSERTION_SIZE.
bool sort(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range range) {
    size_t i, curr_length, curr_size;
    /* Insertion sort by each tasklet. */
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        mram_read(&input[i], cache, curr_size);
        insertion_sort(cache, curr_length);
        mram_write(cache, &input[i], curr_size);
    }
    barrier_wait(&sort_barrier);  // todo: replaceable by a handshake?
    /* Merge by tasklets. */
    return merge(input, output, cache, range);
}