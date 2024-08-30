/**
 * @file
 * @brief Starting run formation and run copying for MRAM MergeSorts.
**/

#ifndef _MERGE_MRAM_H_
#define _MERGE_MRAM_H_

#include <assert.h>
#include <defs.h>

#include "common.h"
#include "buffers.h"
#include "mram_loop.h"

/// @brief The number of items in the starting runs.
#define STARTING_RUN_LENGTH (TRIPLE_BUFFER_LENGTH)
/// @brief The number of bytes a starting run takes.
#define STARTING_RUN_SIZE (STARTING_RUN_LENGTH << DIV)
static_assert(
    STARTING_RUN_SIZE == DMA_ALIGNED(STARTING_RUN_SIZE),
    "The size of starting runs must be properly aligned for DMAs!"
);
static_assert(
    STARTING_RUN_SIZE <= TRIPLE_BUFFER_SIZE,
    "The starting runs are sorted entirely in WRAM and, thus, must fit in there!"
);

extern triple_buffers buffers[NR_TASKLETS];

/**
 * @brief Scans an MRAM array backwards blockwise,
 * sorts those blocks in WRAM, and writes them back.
 * 
 * @param start The first item of the MRAM array to sort.
 * @param end The last item of said array.
**/
void form_starting_runs(T __mram_ptr *start, T __mram_ptr *end);

/**
 * @brief Copies a sorted MRAM array to another MRAM location.
 * 
 * @param from The first item of the MRAM array to copy.
 * @param to The last item of said array.
 * @param out The new location of the first item to copy.
**/
static inline void copy_run(T __mram_ptr *from, T __mram_ptr *to, T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    mram_range_ptr range = { from, to + 1 };
    T __mram_ptr *i;
    size_t curr_length, curr_size;
    LOOP_ON_MRAM_BL(i, curr_length, curr_size, range, MAX_TRANSFER_LENGTH_TRIPLE) {
        mram_read(i, cache, curr_size);
        mram_write(cache, out, curr_size);
        out += curr_length;
    }
}

#endif  // _MERGE_MRAM_H_
