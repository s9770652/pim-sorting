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

void deplete_reader(T __mram_ptr *output, T *cache, size_t i, T *ptr, seqreader_t *sr,
        const T __mram_ptr *end, size_t written) {
    // Fill `cache` up before writing it to `output`.
    // todo: fill up only as little as possible? Worth the extra checks/calculations? Though might be more costly due to more mram_writes!
    while (i < BLOCK_LENGTH && seqread_tell(ptr, sr) != end) {
        cache[i++] = *ptr;
        ptr = seqread_get(ptr, sizeof(T), sr);
    };
    mram_write(cache, &output[written], ROUND_UP_POW2(i << DIV, 8));
    written += i;
    // If the reader still has unread elements, write them to the cache
    // and then the cache to `output`.
    i = 0;
    while (seqread_tell(ptr, sr) != end) {
        cache[i++] = *ptr;
        ptr = seqread_get(ptr, sizeof(T), sr);
        if (i == BLOCK_LENGTH) {
            mram_write(cache, &output[written], BLOCK_SIZE);
            written += BLOCK_LENGTH;
            i = 0;
        }
    }
    // If the cache is not full (because the reader did not read
    // a multiple of `BLOCK_LENGTH` many elements), write the remainder to `output`.
    if (i != 0) {
        mram_write(cache, &output[written], ROUND_UP_POW2(i << DIV, 8));
    }
}

bool merge(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range range) {
    seqreader_buffer_t buffers[2] = { seqread_alloc(), seqread_alloc() };
    seqreader_t sr[2];
    bool flipped = false;  // Whether `input` or `output` contain the sorted elements.
    for (size_t run = BLOCK_LENGTH; run < range.end - range.start; run <<= 1) {
        for (size_t j = range.start; j < range.end; j += run << 1) {
            // If it is just one run, there is nothing to merge.
            // Just move its elements immediately from `input` to `output`.
            if (j + run >= range.end) {
                T *ptr = seqread_init(buffers[0], &input[j], &sr[0]);
                deplete_reader(&output[j], cache, 0, ptr, &sr[0], &input[range.end], 0);
                break;
            }
            // Otherwise, merging is needed.
            T *ptr[2] = {
                seqread_init(buffers[0], &input[j], &sr[0]),
                seqread_init(buffers[1], &input[j + run], &sr[1])
            };
            const T __mram_ptr *ends[2] = {
                &input[j + run],
                &input[(j + (run << 1) <= range.end) ? j + (run << 1) : range.end ]
            };
            bool active = 0;
            size_t i = 0, written = 0;
            while (1) {
                if (*ptr[!active] < *ptr[active]) {
                    active = !active;
                }
                cache[i++] = *ptr[active];
                ptr[active] = seqread_get(ptr[active], sizeof(T), &sr[active]);
                // If a reader reached its end, deplete the other one without further comparisons.
                if (seqread_tell(ptr[active], &sr[active]) == ends[active]) {
                    deplete_reader(&output[j], cache, i, ptr[!active], &sr[!active], ends[!active], written);
                    break;
                }
                // If the cache is full, write its content to `output`.
                if (i == BLOCK_LENGTH) {
                    mram_write(cache, &output[j + written], BLOCK_SIZE);
                    i = 0;
                    written += BLOCK_LENGTH;
                }
            }
        }
        // Flip `input` and `output`. Thus, `input` will always contain the biggest runs.
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