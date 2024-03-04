#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <defs.h>
#include <handshake.h>
#include <mram.h>
#include <seqread.h>

#include "../support/common.h"
#include "mram_loop.h"
#include "sort.h"

#define INSERTION_SIZE SEQREAD_CACHE_SIZE


/**
 * @brief A standard implementation of InsertionSort.
 * @attention This algorithm relies on `array[-1]` equaling the sentinel value `MIN_VALUE`.
 * 
 * @param array The WRAM array to sort.
 * @param length The length of the array.
**/
static void insertion_sort(T *array, size_t const length) {
    for (size_t i = 1; i < length; i++) {
        T *curr = &array[i];
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

// Inlining actually worsens performance.
static __noinline void deplete(T __mram_ptr *input, T __mram_ptr *output, T *cache, T *ptr,
        size_t i, T __mram_ptr const *end) {
    // Transfer cache to MRAM.
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        cache[i++] = *ptr++;  // Possible since `ptr` must have at least one element.
        if (++input == end) {
            mram_write(cache, output, i << DIV);
            return;
        };
    }
#endif
    mram_write(cache, output, i << DIV);
    output += i;

    // Transfer from MRAM to MRAM.
    do {
        // Thanks to the dummy values, even for numbers smaller than 8 bytes,
        // there is no need to round the size up.
        size_t rem_size = (input + BLOCK_LENGTH > end) ? (size_t)end - (size_t)input : BLOCK_SIZE;
        mram_read(input, cache, rem_size);
        mram_write(cache, output, rem_size);
        input += BLOCK_LENGTH;  // Value may be wrong for the last transfer …
        output += BLOCK_LENGTH;  // … after which it is not needed anymore, however.
    } while (input < end);
}

bool merge(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range ranges[NR_TASKLETS]) {
    seqreader_buffer_t buffers[2] = { seqread_alloc(), seqread_alloc() };
    seqreader_t sr[2];
    bool flipped = false;  // Whether `input` or `output` contain the sorted elements.
    size_t initial_run_length = BLOCK_LENGTH;
    mram_range range = { ranges[me()].start, ranges[me()].end };
    // `brother_mask` is used to determine the brother node in the tournament tree.
    for (size_t brother_mask = 1; true; brother_mask <<= 1) {
        for (size_t run = initial_run_length; run < range.end - range.start; run <<= 1) {
            for (size_t j = range.start; j < range.end; j += run << 1) {
                // If it is just one run, there is nothing to merge.
                // Just move its elements immediately from `input` to `output`.
                if (j + run >= range.end) {
                    size_t i, curr_length, curr_size;
                    mram_range deplete_range = { j, range.end };
                    LOOP_ON_MRAM(i, curr_length, curr_size, deplete_range) {
                        mram_read(&input[i], cache, curr_size);
                        mram_write(cache, &output[i], curr_size);
                    }
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
                size_t elems_left[2] = { run, ends[1] - ends[0] };
                size_t i = 0, written = 0;
                while (true) {
                    #define INSERT(ACT, PAS)                                                                            \
                    cache[i++] = *ptr[ACT];                                                                             \
                    ptr[ACT] = seqread_get(ptr[ACT], sizeof(T), &sr[ACT]);                                              \
                    if (--elems_left[ACT] == 0) {                                                                       \
                        deplete(seqread_tell(ptr[PAS], &sr[PAS]), &output[j + written], cache, ptr[PAS], i, ends[PAS]); \
                        break;                                                                                          \
                    }
                    if (*ptr[0] < *ptr[1]) {
                        INSERT(0, 1);
                    } else {
                        INSERT(1, 0);
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
        if ((me() & brother_mask)) {
            handshake_notify();
            break;
        } else {
            if (brother_mask == NR_TASKLETS) break;
            handshake_wait_for(me() | brother_mask);
        }
        initial_run_length = range.end - range.start;
        range.end = ranges[ me() | ((brother_mask << 1) - 1) ].end;
    }
    return flipped;
}

// todo: Adher to INSERTION_SIZE.
bool sort(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range ranges[NR_TASKLETS]) {
    size_t i, curr_length, curr_size;
    /* Insertion sort by each tasklet. */
    LOOP_ON_MRAM(i, curr_length, curr_size, ranges[me()]) {
        mram_read(&input[i], cache, curr_size);
        insertion_sort(cache, curr_length);
        mram_write(cache, &input[i], curr_size);
    }
    /* Merge by tasklets. */
    return merge(input, output, cache, ranges);
}