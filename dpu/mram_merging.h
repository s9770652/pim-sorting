/**
 * @file
 * @brief Merging two given MRAM runs using straight readers.
 * Neither the sizes of the runs nor their addresses nor the output location
 * have to be multiples of 8.
**/

#ifndef _MRAM_MERGING_H_
#define _MRAM_MERGING_H_

#include <assert.h>
#include <stdbool.h>

#include <defs.h>

#include "buffers.h"
#include "common.h"
#include "reader.h"
#include "starting_runs.h"

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

#if UINT32

/**
 * @brief Copy the remainder of a run from the MRAM to the output.
 * 
 * @param from The MRAM address of the current item of the run.
 * @param to The MRAM address of the last item of the run.
 * @param out Whither to flush.
**/
static inline void flush_run(T __mram_ptr *from, T __mram_ptr const *to, T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    // The last element which can be read via `mram_read`.
    T __mram_ptr const * const to_aligned = ((uintptr_t)to & DMA_OFF_MASK) ? to : to - 1;
    size_t rem_size = MAX_TRANSFER_SIZE_TRIPLE;  // size of what is loaded
    size_t rem_length = MAX_TRANSFER_LENGTH_TRIPLE;  // length of what is loaded
    if ((uintptr_t)from & DMA_OFF_MASK) {
        size_t rem_size_shifted = rem_size - 2 * sizeof(T);  // size of what is stored
        size_t rem_length_shifted = rem_size_shifted / sizeof(T);  // length of what is stored
        from--;  // From now on, `from` is already merged, `from + 1` is not.
        while (from < to_aligned - 1) {
            if (from + rem_length_shifted > to_aligned) {
                rem_size = (size_t)to_aligned - (size_t)from + sizeof(T);
                rem_length = rem_size / sizeof(T);
                rem_size_shifted = rem_size - 2 * sizeof(T);
                rem_length_shifted = rem_size_shifted / sizeof(T);
            }
            mram_read(from, cache, rem_size);
            // `rem_length` is always even and the last two elements are not transferred.
            // Therefore, a step size of 2 covers everything needed.
            for (size_t i = 1; i < rem_length_shifted; i += 2) {
                cache[i - 1] = cache[i];
                cache[i] = cache[i + 1];
            }
            mram_write(cache, out, rem_size_shifted);
            from += rem_length_shifted;
            out += rem_length_shifted;
        };
        while ((from + 1) <= to) {
            *out++ = *++from;
        }
    } else {
        while (from < to_aligned) {
            if (from + MAX_TRANSFER_LENGTH_TRIPLE > to_aligned) {
                rem_size = (size_t)to_aligned - (size_t)from + sizeof(T);
                rem_length = rem_size / sizeof(T);
            }
            mram_read(from, cache, rem_size);
            mram_write(cache, out, rem_size);
            from += rem_length;
            out += rem_length;
        };
        while (from <= to) {
            *out++ = *from++;
        }
    }
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
static __noinline void flush_cache_and_run(T const * const ptr, T __mram_ptr *from,
        T __mram_ptr const *to, T __mram_ptr *out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = *ptr;
        if (from >= to) {
            mram_write(cache, out, i * sizeof(T));
            return;
        }
        from++;
    }
    mram_write(cache, out, i * sizeof(T));
    flush_run(from, to, out + i);
}

#elif UINT64

/**
 * @brief Copy the remainder of a run from the MRAM to the output.
 * 
 * @param from The MRAM address of the current item of the run.
 * @param to The MRAM address of the last item of the run.
 * @param out Whither to flush.
**/
static inline void flush_run(T __mram_ptr *from, T __mram_ptr const *to, T __mram_ptr *out) {
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
static __noinline void flush_cache_and_run(T const * const ptr, T __mram_ptr *from,
        T __mram_ptr const *to, T __mram_ptr *out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
    mram_write(cache, out, i * sizeof(T));
    flush_run(from, to, out + i);
}

#endif  // UINT64

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
void merge_mram(T *ptr[2], T __mram_ptr * const ends[2], T __mram_ptr *out,
        seqreader_buffer_t const wram[2]) {
    (void)wram;
    T * const cache = buffers[me()].cache;
    size_t i = 0;
    T val[2] = { *ptr[0], *ptr[1] };
    uintptr_t mram[2] = { sr[me()][0].mram_addr, sr[me()][1].mram_addr };
#if UINT32  // `out` may not be DMA-aligned, so an item is transferred singularly.
    if ((uintptr_t)out & DMA_OFF_MASK) {
        if (val[0] <= val[1]) {
            *out++ = val[0];
            SR_GET(ptr[0], &sr[me()][0], mram[0], wram[0]);
            val[0] = *ptr[0];
        } else {
            *out++ = val[1];
            SR_GET(ptr[1], &sr[me()][1], mram[1], wram[1]);
            val[1] = *ptr[1];
        }
    }
#endif
    if (*ends[0] <= *ends[1]) {
        T __mram_ptr * const early_end = ends[0] - UNROLL_FACTOR + 1;
        while ((intptr_t)sr_tell(ptr[0], &sr[me()][0], mram[0]) < (intptr_t)early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                if (sr_tell(ptr[0], &sr[me()][0], mram[0]) >= ends[0]) {
                    flush_cache_and_run(
                        ptr[1],
                        sr_tell(ptr[1], &sr[me()][1], mram[1]),
                        ends[1],
                        out,
                        i
                    );
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
                    flush_cache_and_run(
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
