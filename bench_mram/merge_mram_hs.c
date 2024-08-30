/**
 * @file
 * @brief Measuring runtimes of a half-space MergeSort (sequential, MRAM, custom readers).
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
#include "reader.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

/// @brief How many items are merged in an unrolled fashion.
#define UNROLL_FACTOR (8)
/// @brief How many items the cache holds before they are written to the MRAM.
/// @internal Due to the unrolling, medium sizes are better than the maximum size.
#define UNROLLING_CACHE_LENGTH (MIN(256, MAX_TRANSFER_LENGTH_CACHE) / UNROLL_FACTOR * UNROLL_FACTOR)
/// @brief How many bytes the items the cache holds before they are written to the MRAM have.
#define UNROLLING_CACHE_SIZE (UNROLLING_CACHE_LENGTH << DIV)

/**
 * @brief Flushes the first run in a pair of runs once the tail of the second one is reached.
 * This includes writing whatever is still in the cache to the MRAM
 * and copying whatever is still in the copy of the first run back to the input.
 * @todo Flush whatever is in the reader?
 * 
 * @param reader A reader on the first run.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static void flush_first(struct reader * const reader, T __mram_ptr *out, size_t i) {
    T * const cache = buffers[me()].cache;
    T __mram_ptr *from = get_reader_mram_address(reader);
    /* Transfer cache to MRAM. */
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = get_reader_value(reader);
        if (from == reader->to) {
            mram_write(cache, out, i * sizeof(T));
            return;
        }
        from++;
    }
#endif
    mram_write(cache, out, i * sizeof(T));
    out += i;

    /* Transfer from MRAM to MRAM. */
    do {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        size_t const rem_size = (from + MAX_TRANSFER_LENGTH_TRIPLE > reader->to)
                ? (size_t)reader->to - (size_t)from + sizeof(T)
                : MAX_TRANSFER_SIZE_TRIPLE;
        mram_read(from, cache, rem_size);
        mram_write(cache, out, rem_size);
        from += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    } while (from <= reader->to);
}

/**
 * @brief Flushes the first run in a pair of runs once the tail of the second one is reached.
 * This includes only copying whatever is still in the copy of the first run back to the input.
 * 
 * @param reader A reader on the first run.
 * @param out Whither to flush.
**/
static void flush_first_emptied_cache(struct reader * const reader, T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    T __mram_ptr *from = get_reader_mram_address(reader);
    /* Transfer from MRAM to MRAM. */
    do {
        // Thanks to the dummy values, even for numbers smaller than `DMA_ALIGNMENT` bytes,
        // there is no need to round the size up.
        size_t const rem_size = (from + MAX_TRANSFER_LENGTH_TRIPLE > reader->to)
                ? (size_t)reader->to - (size_t)from + sizeof(T)
                : MAX_TRANSFER_SIZE_TRIPLE;
        mram_read(from, cache, rem_size);
        mram_write(cache, out, rem_size);
        from += MAX_TRANSFER_LENGTH_TRIPLE;  // Value may be wrong for the last transfer …
        out += MAX_TRANSFER_LENGTH_TRIPLE;  // … after which it is not needed anymore, however.
    } while (from <= reader->to);
}

/**
 * @brief Flushes the second run in a pair of runs once the tail of the first one is reached.
 * This includes only writing whatever is still in the cache to the MRAM.
 * 
 * @param reader A reader on the second run.
 * @param out Whither to flush.
 * @param i The number of items currently in the cache.
**/
static void flush_second(struct reader * const reader, T __mram_ptr * const out, size_t i) {
    T * const cache = buffers[me()].cache;
    (void)reader;
    /* Transfer cache to MRAM. */
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        // This is easily possible since the non-depleted run must have at least one more item.
        cache[i++] = get_reader_value(reader);
    }
#endif
    mram_write(cache, out, i * sizeof(T));
}

/**
 * @brief Merges the `UNROLLING_CACHE_LENGTH` least items in the current pair of runs.
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
#define UNROLLED_MERGE(flush_0, flush_1)                                            \
for (size_t j = 0; j < UNROLLING_CACHE_LENGTH / UNROLL_FACTOR; j++) {               \
    if (!is_early_end_reached(&readers[0]) && !is_early_end_reached(&readers[1])) { \
        _Pragma("unroll")                                                           \
        for (size_t k = 0; k < UNROLL_FACTOR; k++) {                                \
            if (get_reader_value(&readers[0]) <= get_reader_value(&readers[1])) {   \
                cache[i++] = get_reader_value(&readers[0]);                         \
                flush_0;                                                            \
                update_reader_partially(&readers[0]);                               \
            } else {                                                                \
                cache[i++] = get_reader_value(&readers[1]);                         \
                flush_1;                                                            \
                update_reader_partially(&readers[1]);                               \
            }                                                                       \
        }                                                                           \
    } else {                                                                        \
        _Pragma("unroll")                                                           \
        for (size_t k = 0; k < UNROLL_FACTOR; k++) {                                \
            if (get_reader_value(&readers[0]) <= get_reader_value(&readers[1])) {   \
                cache[i++] = get_reader_value(&readers[0]);                         \
                flush_0;                                                            \
                update_reader_fully(&readers[0]);                                   \
            } else {                                                                \
                cache[i++] = get_reader_value(&readers[1]);                         \
                flush_1;                                                            \
                update_reader_fully(&readers[1]);                                   \
            }                                                                       \
        }                                                                           \
    }                                                                               \
}

/**
 * @brief Merges the `UNROLLING_CACHE_LENGTH` least items in the current pair of runs and
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
mram_write(cache, out, UNROLLING_CACHE_SIZE);    \
i = 0;                                           \
out += UNROLLING_CACHE_LENGTH;

/**
 * @brief Merges two MRAM runs. If the second run is depleted, the first one will not be flushed.
 *
 * @param readers A pair of readers on the runs
 * @param out Whither the merged runs are written.
**/
static void merge_half_space(struct reader readers[2], T __mram_ptr *out) {
    T * const cache = buffers[me()].cache;
    size_t i = 0;
    if (*readers[0].to <= *readers[1].to) {
        while (items_left_in_reader(&readers[0]) >= UNROLLING_CACHE_LENGTH) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        if (was_last_item_read(&readers[0])) {
            return;  // Only a `return` since `MERGE_WITH_CACHE_FLUSH` already ended with a flush.
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                if (is_current_item_the_last_one(&readers[0])) {
                    flush_second(&readers[1], out, i);
                    return;
                },
                {}
            );
        }
    } else {
        while (items_left_in_reader(&readers[1]) >= UNROLLING_CACHE_LENGTH) {
            MERGE_WITH_CACHE_FLUSH({}, {});
        }
        if (was_last_item_read(&readers[1])) {
            flush_first_emptied_cache(&readers[0], out);
            return;
        }
        while (true) {
            MERGE_WITH_CACHE_FLUSH(
                {},
                if (is_current_item_the_last_one(&readers[1])) {
                    flush_first(&readers[0], out, i);
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
    struct reader readers[2];
    setup_reader(&readers[0], buffers[me()].seq_1, UNROLL_FACTOR);
    setup_reader(&readers[1], buffers[me()].seq_2, UNROLL_FACTOR);
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
            reset_reader(&readers[0], out, out + (run_1_end - run_1_start));
            reset_reader(&readers[1], run_1_end + 1, run_2_end);
            merge_half_space(readers, run_1_start);
        }
    }
}

union algo_to_test __host algos[] = {
    {{ "MergeHSCustom", { .mram = merge_sort_half_space } }},
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
