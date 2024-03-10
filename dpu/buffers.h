/**
 * @file
 * @brief Allocation of contingous WRAM buffers.
 * 
 * This code allows to allocate a general-purpose WRAM buffer (“cache”) of size `BLOCK_SIZE`
 * and two buffers for sequential readers. It is guaranteed that from the start of the cache
 * up until the end of the second sequential-read buffer, no other tasklet stores anything.
 * Thus, if the sequential-read buffers are not needed, they too can be used for general purposes.
 * The cache then has a size of `BLOCK_SIZE` + 4 × `SEQREAD_CACHE_SIZE`.
**/

#ifndef _BUFFERS_H_
#define _BUFFERS_H_

#include <stddef.h>

#include <seqread.h>

#include "../support/common.h"


/**
 * @brief Holds the WRAM addresses of one general-purpose buffer and two sequential-read buffers.
 * They are contiguous so they can be seen as a single buffer,
 * ranging from `&cache[0]` to `&cache[get_full_buffer_size()]`.
**/
typedef struct wram_buffers {
    /// @brief The general-purpose buffer of size `BLOCK_SIZE`.
    T *cache;
    /// @brief The first buffer for some sequential reader.
    seqreader_buffer_t seq_1;
    /// @brief The second buffer for some sequential reader.
    seqreader_buffer_t seq_2;
} wram_buffers;

/**
 * @brief Allocates contiguous memory for general-purpose buffer and two sequential-reader buffers.
 * Also sets a sentinel value for the general-purpose buffer.
 * 
 * @param buffers The struct where the addresses are stored.
**/
void allocate_buffers(wram_buffers *buffers);

/**
 * @brief Returns the (minimum) size of a general-purpose buffer and two sequential-reader buffers.
 * 
 * @return `BLOCK_SIZE` + 4 × `SEQREAD_CACHE_SIZE`
**/
static inline size_t get_full_buffer_size(void) { return BLOCK_SIZE + 4 * SEQREAD_CACHE_SIZE; }

#endif  // _BUFFERS_H_