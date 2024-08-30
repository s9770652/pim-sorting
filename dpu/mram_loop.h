/**
 * @file
 * @brief Simple looping over arrays in MRAM.
**/

#ifndef _MRAM_LOOP_H_
#define _MRAM_LOOP_H_

#include <stddef.h>

#include <attributes.h>
#include <memmram_utils.h>

#include "buffers.h"
#include "common.h"

/**
 * @brief Start (inclusive) and end (exclusive) of the range
 * traversed by a tasklet in `LOOP_ON_MRAM` & Co.
**/
typedef struct mram_range {
    /// @brief The index at which to start.
    size_t start;
    /// @brief The first index to not include.
    size_t end;
} mram_range;

/**
 * @brief Start (inclusive) and end (exclusive) of the range
 * traversed by a tasklet in `LOOP_ON_MRAM` & Co.
**/
typedef struct mram_range_ptr {
    /// @brief The address at which to start.
    T __mram_ptr *start;
    /// @brief The address where to stop (exclusive).
    T __mram_ptr *end;
} mram_range_ptr;

/**
 * @brief Blockwise iterator over the MRAM indices in the range as defined by `range`.
 * Blocks have the length `MAX_TRANSFER_LENGTH_TRIPLE`.
 * 
 * @param i Set to the current index or pointer, i. e. the start of the current block.
 * @param curr_length Set to the length of the current block as number of elements.
 * @param curr_size Set to the size of the current block, properly aligned for DMAs.
 * @param range Start and end of the tasklet’s range.
**/
#define LOOP_ON_MRAM(i, curr_length, curr_size, range)         \
for (                                                          \
    i = range.start,                                           \
    curr_length = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end) \
            ? range.end - i                                    \
            : MAX_TRANSFER_LENGTH_TRIPLE,                      \
    curr_size = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end)   \
            ? DMA_ALIGNED(curr_length * sizeof(T))             \
            : MAX_TRANSFER_SIZE_TRIPLE                         \
    ;                                                          \
    i < range.end                                              \
    ;                                                          \
    i += MAX_TRANSFER_LENGTH_TRIPLE,                           \
    curr_length = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end) \
            ? range.end - i                                    \
            : MAX_TRANSFER_LENGTH_TRIPLE,                      \
    curr_size = (i + MAX_TRANSFER_LENGTH_TRIPLE > range.end)   \
            ? DMA_ALIGNED(curr_length * sizeof(T))             \
            : MAX_TRANSFER_SIZE_TRIPLE                         \
)

/**
 * @brief Blockwise iterator over the MRAM indices in the range as defined by `range`.
 * 
 * @param i Set to the current index or pointer, i. e. the start of the current block.
 * @param curr_length Set to the length of the current block as number of elements.
 * @param curr_size Set to the size of the current block, properly aligned for DMAs.
 * @param range Start and end of the tasklet’s range.
 * @param block_length Number of elements per block.
**/
#define LOOP_ON_MRAM_BL(i, curr_length, curr_size, range, block_length) \
for (                                                                   \
    i = range.start,                                                    \
    curr_length = (i + (block_length) > range.end)                      \
            ? range.end - i                                             \
            : (block_length),                                           \
    curr_size = (i + (block_length) > range.end)                        \
            ? DMA_ALIGNED(curr_length * sizeof(T))                      \
            : (block_length) * sizeof(T)                                \
    ;                                                                   \
    i < range.end                                                       \
    ;                                                                   \
    i += (block_length),                                                \
    curr_length = (i + (block_length) > range.end)                      \
            ? range.end - i                                             \
            : (block_length),                                           \
    curr_size = (i + (block_length) > range.end)                        \
            ? DMA_ALIGNED(curr_length * sizeof(T))                      \
            : (block_length) * sizeof(T)                                \
)

/**
 * @brief Reverse blockwise iterator over the MRAM indices in the range as defined by `range`.
 * 
 * @param i Set to the current index or pointer, i. e. the start of the current block.
 * @param curr_length Set to the length of the current block as number of elements.
 * @param curr_size Set to the size of the current block, properly aligned for DMAs.
 * @param range Start and end of the tasklet’s range.
 * @param block_length Number of elements per block.
**/
#define LOOP_BACKWARDS_ON_MRAM_BL(i, curr_length, curr_size, range, block_length)   \
for (                                                                               \
    curr_length = ((intptr_t)(range.end - (block_length)) >= (intptr_t)range.start) \
            ? (block_length)                                                        \
            : range.end - range.start,                                              \
    curr_size = ((intptr_t)(range.end - (block_length)) >= (intptr_t)range.start)   \
            ? (block_length) * sizeof(T)                                            \
            : DMA_ALIGNED(curr_length * sizeof(T)),                                 \
    i = ((intptr_t)(range.end - (block_length)) >= (intptr_t)range.start)           \
            ? range.end - (block_length)                                            \
            : range.start                                                           \
    ;                                                                               \
    (intptr_t)curr_length > 0                                                       \
    ;                                                                               \
    curr_length = ((intptr_t)(i - (block_length)) >= (intptr_t)range.start)         \
            ? (block_length)                                                        \
            : i - range.start,                                                      \
    curr_size = ((intptr_t)(i - (block_length)) >= (intptr_t)range.start)           \
            ? (block_length) * sizeof(T)                                            \
            : DMA_ALIGNED(curr_length * sizeof(T)),                                 \
    i = ((intptr_t)(i - (block_length)) >= (intptr_t)range.start)                   \
            ? i - (block_length)                                                    \
            : range.start                                                           \
)

#endif  // _MRAM_LOOP_H_
