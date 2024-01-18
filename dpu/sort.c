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

// #define INSERTION_SIZE CACHE_SIZE
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

typedef struct my_reader {
    T __mram_ptr *mram;
    T __dma_aligned *wram;
    size_t ptr;
    size_t length;
    size_t curr_size;
    size_t curr_length; 
    size_t filled;
    bool exhausted;
} my_reader;

inline void reader_init(my_reader *reader, T __mram_ptr *mram, T __dma_aligned *wram, size_t range_length) {
    reader->mram = mram;
    reader->wram = wram;
    reader->ptr = 0;
    reader->length = range_length;
    reader->curr_size = (range_length >= BLOCK_LENGTH) ? BLOCK_SIZE : ROUND_UP_POW2(range_length << DIV, 8);
    reader->curr_length = (range_length >= BLOCK_LENGTH) ? BLOCK_LENGTH : reader->curr_size >> DIV;  // todo: any gain in just shifting directly  // todo: is the result kept in memory?
    reader->filled = 0;
    reader->exhausted = false;
    mram_read(mram, wram, reader->curr_size);
}

inline T reader_peek(my_reader *reader) {
    return reader->wram[reader->ptr];
}

inline T reader_get(my_reader *reader) {
    T elem = reader->wram[reader->ptr++];
    if (reader->ptr == reader->curr_length) {
        reader->filled += reader->curr_length;
        if (reader->filled < reader->length) {
            reader->ptr = 0;
            reader->curr_size = (reader->length >= reader->filled + BLOCK_LENGTH) ? BLOCK_SIZE : ROUND_UP_POW2((reader->length - reader->filled) << DIV, 8);
            reader->curr_length = (reader->length >= reader->filled + BLOCK_LENGTH) ? BLOCK_LENGTH : reader->curr_size >> DIV;
            mram_read(&reader->mram[reader->filled], reader->wram, reader->curr_size);
        } else {
            reader->exhausted = true;
        }
    }
    return elem;
}

inline T reader_get_no_checks(my_reader *reader) {
    return reader->wram[reader->ptr++];
}

inline bool reader_is_exhausted(my_reader *reader) {
    return reader->exhausted;
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
    seqreader_buffer_t buffers[2] = { seqread_alloc(), seqread_alloc() };
    seqreader_t sr[2];
    T *out = cache;
    bool flipped = false;
    for (uint32_t run = BLOCK_LENGTH; run < range.end - range.start; run <<= 1) {
        for (uint32_t j = range.start; j < range.end; j += run << 1) {
            T *ptr[2] = {
                seqread_init(buffers[0], &input[j], &sr[0]),
                seqread_init(buffers[1], &input[j + run], &sr[1])
            };
            const T __mram_ptr *ends[2] = { &input[j + run - 1], &input[j + run + run - 1] };
            bool active = 0;
            size_t ptr_out = 0, filled_out = 0;
            while (1) {
                if (*ptr[!active] < *ptr[active]) {
                    active = !active;
                }
                out[ptr_out++] = *ptr[active];
                if (seqread_tell(ptr[active], &sr[active]) == ends[active]) {
                    // Fill `cache_out` up so that both it and the rest of `cache_in2` have a size aligned on 8 bytes.
                    // todo: fill up only as little as possible? Worth the extra checks/calculations? Though might be more costly due to more mram_writes!
                    for (uint32_t rest = ptr_out; rest < BLOCK_LENGTH; rest++) {
                        out[rest] = *ptr[!active];
                        ptr[!active] = seqread_get(ptr[!active], sizeof(T), &sr[!active]);
                    }
                    // Empty `cache_out`.
                    mram_write(out, &output[j + filled_out], BLOCK_SIZE);
                    filled_out += BLOCK_LENGTH;
                    // Empty `ptr[!active]`.
                    ptr_out = 0;
                    while (seqread_tell(ptr[!active], &sr[!active]) != ends[!active]+1) {
                        out[ptr_out++] = *ptr[!active];
                        ptr[!active] = seqread_get(ptr[!active], sizeof(T), &sr[!active]);
                        if (ptr_out == BLOCK_LENGTH) {
                            mram_write(out, &output[j + filled_out], BLOCK_SIZE);
                            filled_out += BLOCK_LENGTH;
                            ptr_out = 0;
                        }
                    }
                    if (filled_out != (run << 1)) {
                        mram_write(out, &output[j + filled_out], (BLOCK_LENGTH - ptr_out) << DIV);
                    }
                    break;
                }
                if (ptr_out == BLOCK_LENGTH) {
                    mram_write(out, &output[j + filled_out], BLOCK_SIZE);
                    ptr_out = 0;
                    filled_out += BLOCK_LENGTH;
                }
                ptr[active] = seqread_get(ptr[active], sizeof(T), &sr[active]);
            }
        }
        T __mram_ptr *tmp = input;
        input = output;
        output = tmp;
        flipped = !flipped;
    }


    // /* Merge by tasklets. */
    // T *out = &cache[2 * BLOCK_LENGTH];
    // my_reader *in1 = mem_alloc(sizeof *in1), *in2 = mem_alloc(sizeof *in2);
    // bool flipped = false;
    // for (uint32_t run = BLOCK_LENGTH; run < range.end - range.start; run <<= 1) {
    //     for (uint32_t j = range.start; j < range.end; j += run << 1) {
    //         reader_init(in1, &input[j], cache, run);
    //         reader_init(in2, &input[run + j], &cache[BLOCK_LENGTH], run);
    //         size_t ptr_out = 0, filled_out = 0;
    //         while (1) {
    //             if (reader_peek(in2) < reader_peek(in1)) {
    //                 my_reader *tmp = in1;
    //                 in1 = in2;
    //                 in2 = tmp;
    //             }
    //             out[ptr_out++] = reader_get(in1);
    //             if (reader_is_exhausted(in1)) {
    //                 // Fill `cache_out` up so that both it and the rest of `cache_in2` have a size aligned on 8 bytes.
    //                 // todo: fill up only as little as possible? Worth the extra checks/calculations? Though might be more costly due to more mram_writes!
    //                 for (uint32_t rest = ptr_out; rest < BLOCK_LENGTH; rest++) {
    //                     out[rest] = reader_get_no_checks(in2);
    //                 }
    //                 // Empty `cache_out`.
    //                 mram_write(out, &output[j + filled_out], BLOCK_SIZE);
    //                 filled_out += BLOCK_LENGTH;
    //                 // Empty `in2`.
    //                 mram_write(&in2->wram[in2->ptr], &output[j + filled_out], (in2->curr_length - in2->ptr) << DIV);
    //                 filled_out += in2->curr_length - in2->ptr;
    //                 in2->filled += in2->curr_length;
    //                 if (in2->filled == in2->length) {
    //                     in2->exhausted = true;
    //                 }
    //                 // Move remaining elements from `input` to `output`.
    //                 if (!reader_is_exhausted(in2)) {
    //                     mram_range fill_range = { in2->filled, run, run };
    //                     LOOP_ON_MRAM(i, curr_length, curr_size, fill_range) {
    //                         mram_read(&in2->mram[i], in2->wram, curr_size);
    //                         mram_write(in2->wram, &output[j + filled_out], curr_size);
    //                         filled_out += curr_length;
    //                     }
    //                 }
    //                 break;
    //             }
    //             if (ptr_out == BLOCK_LENGTH) {
    //                 mram_write(out, &output[j + filled_out], BLOCK_SIZE);
    //                 ptr_out = 0;
    //                 filled_out += BLOCK_LENGTH;
    //             }
    //         }
    //     }
    //     T __mram_ptr *tmp = input;
    //     input = output;
    //     output = tmp;
    //     flipped = !flipped;
    // }

    // /* Merge by tasklets. */
    // T *in1, *in2, *out = &cache[2 * BLOCK_LENGTH];
    // size_t *ptr1, *ptr2, *filled1, *filled2;
    // size_t offset1, offset2;
    // bool flipped = false;
    // for (uint32_t run = BLOCK_LENGTH; run < range.end - range.start; run <<= 1) {
    //     for (uint32_t j = range.start; j < range.end; j += run << 1) {
    //         mram_read(&input[j], cache, BLOCK_SIZE);
    //         mram_read(&input[run + j], &cache[BLOCK_LENGTH], BLOCK_SIZE);

    //         in1 = cache; in2 = &cache[BLOCK_LENGTH];
    //         size_t ptr[2] = { 0, 0 };
    //         size_t filled[] = { 0, 0 };
    //         size_t ptr_out = 0, filled_out = 0;
    //         while (1) {
    //             if (cache[ptr[0]] <= cache[BLOCK_LENGTH + ptr[1]]) {
    //                 in1 = cache; in2 = &cache[BLOCK_LENGTH];
    //                 ptr1 = &ptr[0]; ptr2 = &ptr[1];
    //                 filled1 = &filled[0]; filled2 = &filled[1];
    //                 offset1 = 0; offset2 = run;
    //             } else {
    //                 in1 = &cache[BLOCK_LENGTH]; in2 = cache;
    //                 ptr1 = &ptr[1]; ptr2 = &ptr[0];
    //                 filled2 = &filled[0]; filled1 = &filled[1];
    //                 offset1 = run; offset2 = 0;
    //             }
    //             out[ptr_out++] = in1[(*ptr1)++];
    //             if (*ptr1 == BLOCK_LENGTH) {
    //                 *filled1 += BLOCK_LENGTH;
    //                 if (*filled1 < run) {
    //                     mram_read(&input[offset1 + j + *filled1], in1, BLOCK_SIZE);
    //                     *ptr1 = 0;
    //                 } else {
    //                     // Fill `cache_out` up so that both it and the rest of `cache_in2` have a size aligned on 8 bytes.
    //                     for (uint32_t rest = ptr_out; rest < BLOCK_LENGTH; rest++) {
    //                         out[rest] = in2[(*ptr2)++];
    //                     }
    //                     // Empty `cache_out`.
    //                     mram_write(out, &output[j + filled_out], BLOCK_SIZE);
    //                     filled_out += BLOCK_LENGTH;
    //                     // Empty `cache_in2`.
    //                     mram_write(&in2[*ptr2], &output[j + filled_out], (BLOCK_LENGTH - *ptr2) << DIV);
    //                     filled_out += BLOCK_LENGTH - *ptr2;
    //                     *filled2 += BLOCK_LENGTH;
    //                     // Move remaining elements from `input` to `output`.
    //                     if (*filled2 < run) {
    //                         mram_range fill_range = { *filled2, run, run };
    //                         LOOP_ON_MRAM(i, curr_length, curr_size, fill_range) {
    //                             mram_read(&input[offset2 + j + i], in2, curr_size);
    //                             mram_write(in2, &output[j + filled_out], curr_size);
    //                             filled_out += curr_length;
    //                         }
    //                     }
    //                     break;
    //                 }
    //             }
    //             if (ptr_out == BLOCK_LENGTH) {
    //                 mram_write(out, &output[j + filled_out], BLOCK_SIZE);
    //                 ptr_out = 0;
    //                 filled_out += BLOCK_LENGTH;
    //             }
    //         }
    //     }
    //     __mram_ptr T *tmp = input;
    //     input = output;
    //     output = tmp;
    //     flipped = !flipped;
    // }
    return flipped;
}