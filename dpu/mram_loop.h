/**
 * @file
 * @brief Simple looping over arrays in MRAM.
**/

#ifndef _MRAM_LOOP_H_
#define _MRAM_LOOP_H_

#include <stddef.h>

#include <attributes.h>

#include "buffers.h"
#include "common.h"

/**
 * @brief Start (inclusive) and end (exclusive) of the range
 * traversed by a tasklet in `LOOP_ON_MRAM`.
**/
typedef struct mram_range {
    /// @brief The index at which to start.
    size_t start;
    /// @brief The first index to not include.
    size_t end;
} mram_range;

typedef struct mram_range_ptr {
    /// @brief The pointer at which to start.
    T __mram_ptr *start;
    /// @brief The pointer where to stop (inclusive).
    T __mram_ptr *end;
} mram_range_ptr;

/**
 * @brief Blockwise iterator over the MRAM indices in the range as defined by `range`.
 * 
 * @param i Current index, i. e. the start of the current block.
 * @param curr_length Length of the current block as number of elements.
 * @param curr_size Size of the current block, aligned on 8 bytes.
 * @param range Start and end of the tasklet’s range.
**/
#define LOOP_ON_MRAM(i, curr_length, curr_size, range)         \
for (                                                          \
    i = range.start,                                           \
    curr_length = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end) \
            ? range.end - i                                    \
            : MAX_TRANSFER_LENGTH_TRIPLE,                      \
    curr_size = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end)   \
            ? ROUND_UP_POW2(curr_length << DIV, 8)             \
            : MAX_TRANSFER_SIZE_TRIPLE                         \
    ;                                                          \
    i < range.end                                              \
    ;                                                          \
    i += MAX_TRANSFER_LENGTH_TRIPLE,                           \
    curr_length = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end) \
            ? range.end - i                                    \
            : MAX_TRANSFER_LENGTH_TRIPLE,                      \
    curr_size = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end)   \
            ? ROUND_UP_POW2(curr_length << DIV, 8)             \
            : MAX_TRANSFER_SIZE_TRIPLE                         \
)

#define LOOP_ON_MRAM_BL(i, curr_length, curr_size, range, block_length) \
for (                                                                   \
    i = range.start,                                                    \
    curr_length = (i + (block_length) > range.end)                      \
            ? range.end - i                                             \
            : (block_length),                                           \
    curr_size = (i + (block_length) > range.end)                        \
            ? ROUND_UP_POW2(curr_length << DIV, 8)                      \
            : (block_length) << DIV                                     \
    ;                                                                   \
    i < range.end                                                       \
    ;                                                                   \
    i += (block_length),                                                \
    curr_length = (i + (block_length) > range.end)                      \
            ? range.end - i                                             \
            : (block_length),                                           \
    curr_size = (i + (block_length) > range.end)                        \
            ? ROUND_UP_POW2(curr_length << DIV, 8)                      \
            : (block_length) << DIV                                     \
)

#endif  // _MRAM_LOOP_H_
