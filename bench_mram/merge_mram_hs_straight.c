/**
 * @file
 * @brief Measuring runtimes of a half-space MergeSort (sequential, MRAM, regular readers).
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <alloc.h>
#include <defs.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "buffers.h"
#include "checkers.h"
#include "common.h"
#include "communication.h"
#include "random_distribution.h"
#include "random_generator.h"

#include "merge_mram.h"
#include "reader_straight.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

seqreader_t sr[NR_TASKLETS][2];  // sequential readers used to read runs

/// @brief How many items are merged in an unrolled fashion.
#define UNROLL_FACTOR (16)
/// @brief How many items the cache holds before they are written to the MRAM.
#define MAX_FILL_LENGTH (MAX_TRANSFER_LENGTH_CACHE / UNROLL_FACTOR * UNROLL_FACTOR)
/// @brief How many bytes the items the cache holds before they are written to the MRAM have.
#define MAX_FILL_SIZE (MAX_FILL_LENGTH << DIV)

static_assert(
    UNROLL_FACTOR * sizeof(T) == DMA_ALIGNED(UNROLL_FACTOR * sizeof(T)),
    "`UNROLL_FACTOR * sizeof(T)` must be DMA-aligned "
    "as, otherwise, the quick cache flush after the first tier is not possible."
);

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
static void flush_cache_and_run(T const * const ptr, T __mram_ptr *from, T __mram_ptr const *to,
        T __mram_ptr *out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)ptr;
    /* Transfer cache to MRAM. */
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
    out += i;

    /* Transfer from MRAM to MRAM. */
    size_t rem_size = MAX_TRANSFER_SIZE_TRIPLE;
    do {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        if (from + MAX_TRANSFER_LENGTH_TRIPLE > to) {
            rem_size = (size_t)to - (size_t)from + sizeof(T);
        }
        mram_read(from, cache, rem_size);
        mram_write(cache, out, rem_size);
        from += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    } while (from <= to);
}

/**
 * @brief Copy the remainder of a run from the MRAM to the output.
 * 
 * @param from The MRAM address of the current item of the run.
 * @param to The MRAM address of the last item of the run.
 * @param out Whither to flush.
**/
static void flush_run(T __mram_ptr *from, T __mram_ptr const *to, T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    /* Transfer from MRAM to MRAM. */
    size_t rem_size = MAX_TRANSFER_SIZE_TRIPLE;
    do {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        if (from + MAX_TRANSFER_LENGTH_TRIPLE > to) {
            rem_size = (size_t)to - (size_t)from + sizeof(T);
        }
        mram_read(from, cache, rem_size);
        mram_write(cache, out, rem_size);
        from += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    } while (from <= to);
}

/**
 * @brief Write whatever is still in the cache to the MRAM.
 * 
 * @param ptr The current buffer item of the second.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static void flush_cache(T * const ptr, T __mram_ptr * const out, size_t i) {
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
 * @brief Merges two MRAM runs. If the second run is depleted, the first one will not be flushed.
 * 
 * @param sr Two regular UPMEM readers on the two runs.
 * @param ptr The current buffer items of the runs.
 * @param ends The last items of the two runs.
 * @param out Whither the merged runs are written.
**/
static void merge_half_space(T *ptr[2], T __mram_ptr * const ends[2], T __mram_ptr *out,
        seqreader_buffer_t wram[2]) {
    T * const cache = buffers[me()].cache;
    size_t i = 0;
    T val[2] = { *ptr[0], *ptr[1] };
    uintptr_t mram[2] = { sr[me()][0].mram_addr, sr[me()][1].mram_addr };
    if (*ends[0] <= *ends[1]) {
        T __mram_ptr * const early_end = ends[0] - UNROLL_FACTOR + 1;
        while (sr_tell(ptr[0], &sr[me()][0], mram[0], wram[0]) <= early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        if (sr_tell(ptr[0], &sr[me()][0], mram[0], wram[0]) > ends[0]) {
            // The previous loop was executend an even number of times.
            // Since the first run is emptied and had a DMA-aligned length,
            // `i * sizeof(T)` must also be DMA-aligned
            if (i != 0)
                mram_write(cache, out, i * sizeof(T));
            return;
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                if (sr_tell(ptr[0], &sr[me()][0], mram[0], wram[0]) >= ends[0]) {
                    flush_cache(ptr[1], out, i);
                    return;
                },
                {}
            );
        }
    } else {
        T __mram_ptr * const early_end = ends[1] - UNROLL_FACTOR + 1;
        while (sr_tell(ptr[1], &sr[me()][1], mram[1], wram[1]) <= early_end) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        if (sr_tell(ptr[1], &sr[me()][1], mram[1], wram[1]) > ends[1]) {
            if (i != 0) {
                mram_write(cache, out, i * sizeof(T));
                out += i;
            }
            flush_run(sr_tell(ptr[0], &sr[me()][0], mram[0], wram[0]), ends[0], out);
            return;
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                {},
                if (sr_tell(ptr[1], &sr[me()][1], mram[1], wram[1]) >= ends[1]) {
                    flush_cache_and_run(
                        ptr[0],
                        sr_tell(ptr[0], &sr[me()][0], mram[0], wram[0]),
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

/**
 * @brief An implementation of MergeSort that only uses `n`/2 additional space.
 * 
 * @param start The first item of the MRAM array to sort.
 * @param end The last item of said array.
**/
static void merge_sort_half_space(T __mram_ptr * const start, T __mram_ptr * const end) {
    /* Starting runs. */
    form_starting_runs(start, end);

    /* Merging. */
    seqreader_buffer_t wram[2] = { buffers[me()].seq_1, buffers[me()].seq_2 };
    size_t const n = end - start + 1;
    T __mram_ptr * const out = (T __mram_ptr *)((uintptr_t)output + (uintptr_t)start);
    for (size_t run_length = STARTING_RUN_LENGTH; run_length < n; run_length *= 2) {
        for (
            T __mram_ptr *run_1_end = end - run_length, *run_2_end = end;
            (intptr_t)run_1_end >= (intptr_t)start;
            run_1_end -= 2 * run_length, run_2_end -= 2 * run_length
        ) {
            // Copy the current run …
            T __mram_ptr *run_1_start = ((intptr_t)(run_1_end - run_length + 1) > (intptr_t)start)
                    ? run_1_end - run_length + 1
                    : start;
            copy_run(run_1_start, run_1_end, out);
            // … and merge the copy with the next run.
            T __mram_ptr * const ends[2] = { out + (run_1_end - run_1_start), run_2_end };
            T *ptr[2] = {
                sr_init(buffers[me()].seq_1, out, &sr[me()][0]),
                sr_init(buffers[me()].seq_2, run_1_end + 1, &sr[me()][1]),
            };
            merge_half_space(ptr, ends, run_1_start, wram);
        }
    }
}

union algo_to_test __host algos[] = {
    {{ "MergeHSStraight", { .mram = merge_sort_half_space } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
    if (me() != 0) return EXIT_SUCCESS;

    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 0x1000;
        host_to_dpu.offset = DMA_ALIGNED(host_to_dpu.length * sizeof(T)) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }

    /* Perform test. */
    mram_range range = { 0, host_to_dpu.length };
    sort_algo_mram * const algo = algos[host_to_dpu.algo_index].data.fct.mram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());

        array_stats stats_before;
        get_stats_unsorted(input, cache, range, false, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        time new_time = perfcounter_get();
        algo(&input[range.start], &input[range.end - 1]);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
        dpu_to_host.firsts += new_time;
        dpu_to_host.seconds += new_time * new_time;

        array_stats stats_after;
        get_stats_sorted(input, cache, range, false, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        range.start += host_to_dpu.offset;
        range.end += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}