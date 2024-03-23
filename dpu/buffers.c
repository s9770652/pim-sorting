/**
 * The code commented out with block comments runs faster
 * but involves a lot of duplicated code from `seqread.inc`.
 * Unless the additional speed is required,
 * the version with explicit calls to `seqread_alloc` is recommended.
 * Keep in mind that even that version depends on duplicated code: `PAGE_ALLOC_SIZE`.
**/
#include <assert.h>

#include <alloc.h>
#include <barrier.h>
#include <mutex.h>
#include <seqread.h>
/*
#include <atomic_bit.h>
#include <dpuruntime.h>
*/

#include "buffers.h"

#define PAGE_SIZE (SEQREAD_CACHE_SIZE)
#define PAGE_ALLOC_SIZE (2 * PAGE_SIZE)
/*
#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)

extern void *mem_alloc_nolock(size_t size);
ATOMIC_BIT_EXTERN(__heap_pointer);
*/

BARRIER_INIT(allocating_barrier, NR_TASKLETS);
MUTEX_INIT(allocating_mutex);

void allocate_triple_buffer(triple_buffers *buffers) {
    // This barrier-mutex construction is needed to ensure
    // that no call to `mem_alloc` happens somewhere else in between.
    barrier_wait(&allocating_barrier);
    mutex_lock(allocating_mutex);
    // Initialise a local cache to store one MRAM block.
    // In front of the cache is a sentinel value, useful for sorting and checking the order.
    size_t const sentinel_size = (sizeof(T) >= 8) ? sizeof(T) : 8;
    T *cache_pointer = mem_alloc(BLOCK_SIZE + sentinel_size) + sentinel_size;
    // Initialise the buffers for the two sequential readers.
    buffers->seq_1 = seqread_alloc();
    buffers->seq_2 = seqread_alloc();
    mutex_unlock(allocating_mutex);
    barrier_wait(&allocating_barrier);
    // Set the sentinel value and the missing members of `buffers`.
    *(cache_pointer-1) = T_MIN;
    buffers->cache = cache_pointer;
    // Check if implementation of sequential readers has not changed in a significant way.
    // Oþerwise, `TRIPLE_BUFFER_SIZE` would be wrong.
    assert(buffers->seq_2 - buffers->seq_1 == PAGE_ALLOC_SIZE);

    /*
    ATOMIC_BIT_ACQUIRE(__heap_pointer);

    // Initialise a local cache to store one MRAM block.
    // In front of the cache is a sentinel value, useful for sorting and checking the order.
    size_t const sentinel_size = (sizeof(T) >= 8) ? sizeof(T) : 8;
    T *cache_pointer = mem_alloc_nolock(BLOCK_SIZE + sentinel_size) + sentinel_size;
    *(cache_pointer-1) = T_MIN;

    // Initialise the buffers for the two sequential readers.
    uintptr_t heap_pointer = (uintptr_t)__HEAP_POINTER;
    uintptr_t pointer = (heap_pointer + PAGE_OFF_MASK) & PAGE_IDX_MASK;
    size_t size = pointer + 2 * PAGE_ALLOC_SIZE - heap_pointer;  // `2 *` added
    mem_alloc_nolock(size);

    ATOMIC_BIT_RELEASE(__heap_pointer);

    buffers->cache = cache_pointer;
    buffers->seq_1 = pointer;
    buffers->seq_2 = pointer + PAGE_ALLOC_SIZE;
    */
}