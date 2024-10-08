/**
 * @file
 * @brief Merging two given MRAM runs using straight readers.
 * All runs must have a size which is a multiple of 8.
 * Their starting addresses as well as the output location must be DMA-aligned, too.
 * 
 * Offers the flag `MRAM_MERGE` to switch between full-space and half-space merging.
 * The proper output location must still be provided manually, of course.
**/

#ifndef _MRAM_MERGING_H_
#define _MRAM_MERGING_H_

#include <assert.h>
#include <stdbool.h>

#include <defs.h>

#include "buffers.h"
#include "common.h"
#include "reader.h"

#define FULL_SPACE (1)
#define HALF_SPACE (2)
#if (MRAM_MERGE == FULL_SPACE)

#define FLUSH_AFTER_TIER_1()                                                                 \
flush_run_aligned(sr_tell(ptr[1], &sr[me()][1], mram[1]), ends[1], out)

#define FLUSH_IN_TIER_2()                                                                    \
flush_cache_and_run_aligned(ptr[1], sr_tell(ptr[1], &sr[me()][1], mram[1]), ends[1], out, i)

#elif (MRAM_MERGE == HALF_SPACE)

#define FLUSH_AFTER_TIER_1()

#define FLUSH_IN_TIER_2() flush_cache_aligned(ptr[1], out, i)

#endif  // MRAM_MERGE == HALF_SPACE

/// @brief How many items are merged in an unrolled fashion.
#define UNROLL_FACTOR (8)
/// @brief How many items the cache holds before they are written to the MRAM.
#define MAX_FILL_LENGTH (MAX_TRANSFER_LENGTH_CACHE / UNROLL_FACTOR * UNROLL_FACTOR)
/// @brief How many bytes the items the cache holds before they are written to the MRAM have.
#define MAX_FILL_SIZE (MAX_FILL_LENGTH << DIV)

static_assert(
    UNROLL_FACTOR * sizeof(T) == DMA_ALIGNED(UNROLL_FACTOR * sizeof(T)),
    "`UNROLL_FACTOR * sizeof(T)` must be DMA-aligned "
    "as, otherwise, the quick cache flush after the first tier is not possible."
);

extern triple_buffers buffers[NR_TASKLETS];
extern seqreader_t sr[NR_TASKLETS][2];  // sequential readers used to read runs

/**
 * @brief Write whatever is still in the cache to the MRAM.
 * 
 * @param ptr The current buffer item of the run.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static inline void flush_cache_aligned(T * const ptr, T __mram_ptr * const out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = *ptr;
    }
#endif
    mram_write(cache, out, i * sizeof(T));
}

/**
 * @brief Copy the remainder of a run from the MRAM to the output.
 * 
 * @param from The MRAM address of the current item of the run.
 * @param to The MRAM address of the last item of the run.
 * @param out Whither to flush.
**/
static inline void flush_run_aligned(T __mram_ptr *from, T __mram_ptr const *to,
        T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    size_t rem_size = MAX_TRANSFER_SIZE_TRIPLE;
    while (from <= to) {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        if (from + MAX_TRANSFER_LENGTH_TRIPLE > to) {
            rem_size = (size_t)to - (size_t)from + sizeof(T);
        }
        mram_read(from, cache, rem_size);
        mram_write(cache, out, rem_size);
        from += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    };
}

/**
 * @brief Write whatever is still in the cache to the MRAM.
 * If the given run is not depleted, copy its remainder to the output.
 * 
 * @param ptr The current buffer item of the run.
 * @param from The MRAM address of the current item of the run.
 * @param to The MRAM address of the last item of the run.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static inline void flush_cache_and_run_aligned(T const * const ptr, T __mram_ptr *from,
        T __mram_ptr const * const to, T __mram_ptr *out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = *ptr;
        if (from >= to) {
            mram_write(cache, out, i * sizeof(T));
            return;
        }
        from++;
    }
#endif
    mram_write(cache, out, i * sizeof(T));
    flush_run_aligned(from, to, out + i);
}

/**
 * @brief Merges the `MAX_FILL_LENGTH` least items in the current pair of runs.
 * @internal If one of the runs does not contain sufficiently many items anymore,
 * bounds checks on both runs occur with each itemal merge. The reason is that
 * the unrolling everywhere makes the executable too big if the check is more fine-grained.
 * 
 * @param flush_0 An if block checking whether the tail of the first run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
 * @param flush_1  An if block checking whether the tail of the second run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
**/
#define UNROLLED_MERGE(flush_0, flush_1)                \
_Pragma("unroll")                                       \
for (size_t k = 0; k < UNROLL_FACTOR; k++) {            \
    if (val[0] <= val[1]) {                             \
        cache[i++] = val[0];                            \
        flush_0;                                        \
        SR_GET(ptr[0], &sr[me()][0], mram[0], wram[0]); \
        val[0] = *ptr[0];                               \
    } else {                                            \
        cache[i++] = val[1];                            \
        flush_1;                                        \
        SR_GET(ptr[1], &sr[me()][1], mram[1], wram[1]); \
        val[1] = *ptr[1];                               \
    }                                                   \
}

/**
 * @brief Merges the `MAX_FILL_LENGTH` least items in the current pair of runs and
 * writes them to the MRAM.
 * 
 * @param flush_0 An if block checking whether the tail of the first run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
 * @param flush_1  An if block checking whether the tail of the second run is reached
 * and calling the appropriate flushing function. May be an empty block
 * if it is known that the tail cannot be reached.
**/
#define MERGE_WITH_CACHE_FLUSH(flush_0, flush_1) \
UNROLLED_MERGE(flush_0, flush_1);                \
if (i < MAX_FILL_LENGTH) continue;               \
mram_write(cache, out, MAX_FILL_SIZE);           \
i = 0;                                           \
out += MAX_FILL_LENGTH

/**
 * @brief Merges two MRAM runs.
 * 
 * @param sr Two regular UPMEM readers on the two runs.
 * @param ptr The current buffer items of the runs.
 * @param ends The last items of the two runs.
 * @param out Whither the merged runs are written.
**/
static inline void merge_mram_aligned(T *ptr[2], T __mram_ptr * const ends[2], T __mram_ptr *out,
        seqreader_buffer_t const wram[2]) {
    (void)wram;
    T * const cache = buffers[me()].cache;
    size_t i = 0;
    T val[2] = { *ptr[0], *ptr[1] };
    uintptr_t mram[2] = { sr[me()][0].mram_addr, sr[me()][1].mram_addr };
    if (*ends[0] <= *ends[1]) {
        T __mram_ptr * const early_end = ends[0] - UNROLL_FACTOR + 1;
        while ((intptr_t)sr_tell(ptr[0], &sr[me()][0], mram[0]) < (intptr_t)early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                if (sr_tell(ptr[0], &sr[me()][0], mram[0]) >= ends[0]) {
                    FLUSH_IN_TIER_2();
                    return;
                },
                {}
            );
        }
    } else {
        T __mram_ptr * const early_end = ends[1] - UNROLL_FACTOR + 1;
        while ((intptr_t)sr_tell(ptr[1], &sr[me()][1], mram[1]) < (intptr_t)early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                {},
                if (sr_tell(ptr[1], &sr[me()][1], mram[1]) >= ends[1]) {
                    flush_cache_and_run_aligned(
                        ptr[0],
                        sr_tell(ptr[0], &sr[me()][0], mram[0]),
                        ends[0],
                        out,
                        i
                    );
                    return;
                }
            );
        }
    }
}

#endif  // _MRAM_MERGING_H_
