/**
 * @file
 * @brief Allocation of contingous WRAM buffers.
 * 
 * This code allows to allocate a general-purpose WRAM buffer (“cache”) of size `CACHE_SIZE`
 * and two buffers for sequential readers. It is guaranteed that from the start of the cache
 * up until the end of the second sequential-read buffer, no other tasklet stores anything.
 * Thus, if the sequential-read buffers are not needed, they too can be used for general purposes.
 * The cache then has a size of `CACHE_SIZE` + 4 × `SEQREAD_CACHE_SIZE`.
**/

#ifndef _BUFFERS_H_
#define _BUFFERS_H_

#include <stddef.h>

#include <mram.h>
#include <memmram_utils.h>
#include <seqread.h>

#include "common.h"

/// @brief The minimum size of a general-purpose buffer & two sequential-reader buffers.
#define TRIPLE_BUFFER_SIZE ((CACHE_SIZE + 4 * SEQREAD_CACHE_SIZE) & ~DMA_OFF_MASK)
/// @brief The min. number of elements in a general-purpose buffer & two sequential-reader buffers.
#define TRIPLE_BUFFER_LENGTH (TRIPLE_BUFFER_SIZE >> DIV)
/// @brief The maximum number of bytes transferable at once between MRAM and a triple buffer.
#define MAX_TRANSFER_SIZE_TRIPLE (((TRIPLE_BUFFER_SIZE > 2048) ? 2048 : TRIPLE_BUFFER_SIZE) \
        & ~DMA_OFF_MASK)
/// @brief The maximum number of elements transferable at once between MRAM and a triple buffer.
#define MAX_TRANSFER_LENGTH_TRIPLE (MAX_TRANSFER_SIZE_TRIPLE >> DIV)
/// @brief The maximum number of bytes transferable at once between MRAM and the cache.
#define MAX_TRANSFER_SIZE_CACHE (((CACHE_SIZE > 2048) ? 2048 : CACHE_SIZE) & ~DMA_OFF_MASK)
/// @brief The maximum number of elements transferable at once between MRAM and the cache.
#define MAX_TRANSFER_LENGTH_CACHE (MAX_TRANSFER_SIZE_CACHE >> DIV)

/**
 * @brief Holds the WRAM addresses of one general-purpose buffer and two sequential-read buffers.
 * They are contiguous so they can be seen as a single buffer,
 * ranging from `&cache[0]` to `&cache[TRIPLE_BUFFER_SIZE]`.
**/
typedef struct triple_buffers {
    /// @brief The general-purpose buffer of size `CACHE_SIZE`.
    T *cache;
    /// @brief The first buffer for some sequential reader.
    seqreader_buffer_t seq_1;
    /// @brief The second buffer for some sequential reader.
    seqreader_buffer_t seq_2;
} triple_buffers;

/**
 * @brief Allocates contiguous memory for general-purpose buffer and two sequential-reader buffers.
 * Also sets a sentinel value for the general-purpose buffer.
 * @note It is advisable not to call `mem_alloc`
 * as long as not all tasklets have allocated their triple buffer.
 * Otherwise, the rounding this function does can lead to siginificant waste of memory space.
 * 
 * @param buffers The struct where the addresses are stored.
**/
void allocate_triple_buffer(triple_buffers *buffers);

/**
 * @brief Stores the specified number of bytes from MRAM to a triple buffer in WRAM.
 * 
 * @param from Source address in MRAM.
 * @param to Destination address of triple buffer in WRAM.
 * @param nb_of_bytes Number of bytes to transfer.
**/
static inline void mram_read_triple(void const __mram_ptr *from, void *to, size_t nb_of_bytes) {
    do {
        size_t const read_size = (nb_of_bytes > MAX_TRANSFER_SIZE_TRIPLE)
                ? MAX_TRANSFER_SIZE_TRIPLE
                : nb_of_bytes;
        mram_read(from, to, read_size);
        from += read_size;
        to += read_size;
        nb_of_bytes -= read_size;
    } while (nb_of_bytes);
}

/**
 * @brief Stores the specified number of bytes from a triple buffer in WRAM to MRAM.
 * 
 * @param from Source address of triple buffer in WRAM.
 * @param to Destination address in MRAM.
 * @param nb_of_bytes Number of bytes to transfer.
**/
static inline void mram_write_triple(void const *from, void __mram_ptr *to, size_t nb_of_bytes) {
    do {
        size_t const read_size = (nb_of_bytes > MAX_TRANSFER_SIZE_TRIPLE)
                ? MAX_TRANSFER_SIZE_TRIPLE
                : nb_of_bytes;
        mram_write(from, to, read_size);
        from += read_size;
        to += read_size;
        nb_of_bytes -= read_size;
    } while (nb_of_bytes);
}

#endif  // _BUFFERS_H_
