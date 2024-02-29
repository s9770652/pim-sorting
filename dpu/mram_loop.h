#ifndef _MRAM_LOOP_H_
#define _MRAM_LOOP_H_

/**
 * @file mram_loop.h
 * @brief Simple looping over arrays in MRAM.
**/

#include <stddef.h>

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

/**
 * @brief Blockwise iterator over the MRAM indices in the range as defined by `range`.
 * 
 * @param i Current index, i. e. the start of the current block.
 * @param curr_length Length of the current block as number of elements.
 * @param curr_size Size of the current block, aligned on 8 bytes.
 * @param range Start and end of the tasklet's range.
**/
#define LOOP_ON_MRAM(i, curr_length, curr_size, range)                                              \
for (                                                                                               \
    i = range.start,                                                                                \
    curr_length = (i + BLOCK_LENGTH > range.end) ? range.end - i : BLOCK_LENGTH,                    \
    curr_size = (i + BLOCK_LENGTH > range.end) ? ROUND_UP_POW2(curr_length << DIV, 8) : BLOCK_SIZE; \
    i < range.end;                                                                                  \
    i += BLOCK_LENGTH,                                                                              \
    curr_length = (i + BLOCK_LENGTH > range.end) ? range.end - i : BLOCK_LENGTH,                    \
    curr_size = (i + BLOCK_LENGTH > range.end) ? ROUND_UP_POW2(curr_length << DIV, 8) : BLOCK_SIZE  \
)

#endif  // _MRAM_LOOP_H_