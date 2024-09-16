/**
 * @file
 * @brief Measuring runtimes of MergeSorts (sequential, WRAM).
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
#include "pivot.h"
#include "random_distribution.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

static bool flipped[NR_TASKLETS];  // Whether `output` contains the latest sorted runs.

#define FLUSH_BATCH_LENGTH (24)  // The number of elements flushed at once if possible.

#if (MERGE_THRESHOLD > 48)
#define FIRST_STEP (12)  // The first step size of three-pass ShellSort.
#elif (MERGE_THRESHOLD > 16)
#define FIRST_STEP (6)  // The first step size of two-pass ShellSort.
#else
#define FIRST_STEP (1)  // The first step size of one-pass ShellSort, that is InsertionSort.
#endif

/**
 * @brief An implementation of standard InsertionSort.
 * @attention This algorithm relies on `start[-1]` being a sentinel value,
 * i.e. being at least as small as any value in the array.
 * For this reason, `cache[-1]` is set to `T_MIN`.
 * For QuickSort, the last value of the previous partition takes on that role.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void insertion_sort_sentinel(T * const start, T * const end) {
    T *curr, *i = start + 1;
    while ((curr = i++) <= end) {
        T const to_sort = *curr;
        while (*(curr - 1) > to_sort) {  // `-1` always valid due to the sentinel value
            *curr = *(curr - 1);
            curr--;
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of InsertionSort with variable step size.
 * Needed by ShellSort.
 * @attention This algorithm relies on `start[-step .. -1]` being a sentinel value,
 * i.e. being at least as small as any value in the array.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 * @param step The distance between any two elements compared.
**/
static __attribute__((unused)) void insertion_sort_with_steps_sentinel(T * const start,
        T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) <= end) {
        T const to_sort = *curr;
        while (*(curr - step) > to_sort) {  // `-step` always valid due to the sentinel value
            *curr = *(curr - step);
            curr -= step;
        }
        *curr = to_sort;
    }
}

/**
 * @brief A ShellSort with one, two, or three passes of InsertionSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static __attribute__((unused)) void shell_sort(T * const start, T * const end) {
#if (MERGE_THRESHOLD > 48)
    for (size_t j = 0; j < 12; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 12);
    for (size_t j = 0; j < 5; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 5);
#elif (MERGE_THRESHOLD > 18)
    for (size_t j = 0; j < 6; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 6);
#endif  // MERGE_THRESHOLD > 18
    insertion_sort_sentinel(start, end);
}

/**
 * @brief Creating the starting runs for MergeSort.
 * The runs are formed from right to left, such that the first run may be smaller.
**/
#define FORM_STARTING_RUNS_RIGHT2LEFT()                                        \
if (end - start + 1 <= MERGE_THRESHOLD) {                                      \
    shell_sort(start, end);                                                    \
    flipped[me()] = false;                                                     \
    return;                                                                    \
}                                                                              \
for (T *t = end; t > start; t -= MERGE_THRESHOLD) {                            \
    T *t_ = t - MERGE_THRESHOLD + 1 > start ? t - MERGE_THRESHOLD + 1 : start; \
    T before_sentinel[FIRST_STEP];                                             \
    _Pragma("unroll")  /* Set sentinel values. */                              \
    for (size_t i = 0; i < FIRST_STEP; i++) {                                  \
        before_sentinel[i] = *(t_ - i - 1);                                    \
        *(t_ - i - 1) = T_MIN;                                                 \
    }                                                                          \
    shell_sort(t_, t);                                                         \
    _Pragma("unroll")  /* Restore old values. */                               \
    for (size_t i = 0; i < FIRST_STEP; i++) {                                  \
        *(t_ - i - 1) = before_sentinel[i];                                    \
    }                                                                          \
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using batches of length `FLUSH_BATCH_LENGTH`:
 * If there are at least `FLUSH_BATCH_LENGTH` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `FLUSH_BATCH_LENGTH`th in good cases.
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void flush_batch(T *in, T *until, T *out) {
    while (in + FLUSH_BATCH_LENGTH - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < FLUSH_BATCH_LENGTH; k++)
            *(out + k) = *(in + k);
        out += FLUSH_BATCH_LENGTH;
        in += FLUSH_BATCH_LENGTH;
    }
    while (in <= until) {
        *out++ = *in++;
    }
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using batches of length `MERGE_THRESHOLD`:
 * If there are at least `MERGE_THRESHOLD` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `MERGE_THRESHOLD`th in good cases.
 * @sa copy_full_run
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void copy_run(T *in, T *until, T *out) {
    while (in + MERGE_THRESHOLD - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < MERGE_THRESHOLD; k++)
            *(out + k) = *(in + k);
        out += MERGE_THRESHOLD;
        in += MERGE_THRESHOLD;
    }
    while (in <= until) {
        *out++ = *in++;
    }
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using *only* batches of length `MERGE_THRESHOLD`:
 * If there are at least `MERGE_THRESHOLD` many elements to copy, they are copied at once.
 * For this reason, the length of the buffer must be a multiple of `MERGE_THRESHOLD`.
 * @sa copy_run
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void copy_full_run(T *in, T *until, T *out) {
    while (in + MERGE_THRESHOLD - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < MERGE_THRESHOLD; k++)
            *(out + k) = *(in + k);
        out += MERGE_THRESHOLD;
        in += MERGE_THRESHOLD;
    }
}

/**
 * @brief How many iterations in the mergers are unrolled.
 * @note This value is capped at 16, as otherwise the IRAM is in danger of overflowing.
**/
#define UNROLL_FACTOR (MIN(MERGE_THRESHOLD, 16))

/**
 * @brief Merges the two runs within the merger functions in unrolled loops.
 * 
 * @param ptr The pointer of the run whose last element is less than that of the other run.
 * @param end The address of said last element.
 * @param on_depletion An if-block for when and what to do if said run is fully merged.
**/
#define UNROLLED_MERGER(ptr, end, on_depletion)      \
while (ptr <= end - UNROLL_FACTOR + 1) {             \
    _Pragma("unroll")                                \
    for (size_t k = 0; k < UNROLL_FACTOR; k++) {     \
        if (val_i <= val_j) {                        \
            *(out + k) = val_i;                      \
            val_i = *++i;                            \
        } else {                                     \
            *(out + k) = val_j;                      \
            val_j = *++j;                            \
        }                                            \
    }                                                \
    out += UNROLL_FACTOR;                            \
};                                                   \
on_depletion                                         \
while (ptr <= end - (UNROLL_FACTOR / 2) + 1) {       \
    _Pragma("unroll")                                \
    for (size_t k = 0; k < UNROLL_FACTOR / 2; k++) { \
        if (val_i <= val_j) {                        \
            *(out + k) = val_i;                      \
            val_i = *++i;                            \
        } else {                                     \
            *(out + k) = val_j;                      \
            val_j = *++j;                            \
        }                                            \
    }                                                \
    out += UNROLL_FACTOR / 2;                        \
}                                                    \
on_depletion

/**
 * @brief Merges two runs ranging from [`start_1`, `start_2`[ and [`start_2`, `end_2`].
 * @internal Using `end_1 = start_2 - 1` worsens the runtime
 * but making `end_2` exclusive also worsens the runtime, hence the asymmetry.
 * 
 * @param start_1 The first element of the first run.
 * @param start_2 The first element of the second run. Must follow the end of the first run.
 * @param end_2 The last element of the second run.
 * @param out Whither the merged runs are written.
**/
static inline void merge(T * const start_1, T * const start_2, T * const end_2, T *out) {
    T *i = start_1, *j = start_2;
    T val_i = *i, val_j = *j;
    if (*(start_2 - 1) <= *(end_2)) {
        UNROLLED_MERGER(i, start_2 - 1, if (i == start_2) { flush_batch(j, end_2, out); return; });
        while (true) {
            if (val_i <= val_j) {
                *out++ = val_i;
                val_i = *++i;
                if (i == start_2) {  // Pulling these if-statements out of the loop …
                    flush_batch(j, end_2, out);
                    return;
                }
            } else {
                *out++ = val_j;
                val_j = *++j;
            }
        }
    } else {
        UNROLLED_MERGER(j, end_2, if (j > end_2) { flush_batch(i, start_2 - 1, out); return; });
        while (true) {
            if (val_i <= val_j) {
                *out++ = val_i;
                val_i = *++i;
            } else {
                *out++ = val_j;
                val_j = *++j;
                if (j > end_2) {  // … worsens the runtime.
                    flush_batch(i, start_2 - 1, out);
                    return;
                }
            }
        }
    }
}

/**
 * @brief An implementation of standard MergeSort.
 * @note This function saves up to `n` elements after the end of the input array.
 * For speed reasons, the sorted array may be stored after that very end.
 * In other words, the sorted array is not written back to the start of the input array.
 * @internal `inline` does seem to help‽ I only measured the runtimes, though.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static inline void merge_sort_no_write_back(T * const start, T * const end) {
    /* Starting runs. */
    FORM_STARTING_RUNS_RIGHT2LEFT();

    /* Merging. */
    T *in, *until, *out;  // Runs from `in` to `until` are merged and stored in front of `out`.
    bool flip = false;  // Used to determine the initial positions of `in`, `out`, and `until`.
    size_t const n = end - start + 1;
    for (size_t run_length = MERGE_THRESHOLD; run_length < n; run_length *= 2) {
        // Set the positions to read from and write to.
        if ((flip = !flip)) {
            in = start;
            until = end;
            out = end + n + 1;
        } else {
            in = end + 1;
            until = end + n;
            out = end + 1;
        }
        // Merge pairs of neighboured runs which are all of the same length.
        T *run_1_end = until - run_length;
        for (; (intptr_t)run_1_end >= (intptr_t)(in + run_length - 1); run_1_end -= 2*run_length) {
            out -= 2*run_length;
            merge(run_1_end + 1 - run_length, run_1_end + 1, run_1_end + run_length, out);
        }
        // Merge pair at the beginning where the first run is shorter.
        if ((intptr_t)run_1_end >= (intptr_t)in) {
            size_t const run_1_length = run_1_end + 1 - in;
            out -= run_length + run_1_length;
            merge(in, run_1_end + 1, run_1_end + run_length, out);
        // Flush single run at the beginning straight away
        } else if ((intptr_t)(run_1_end + run_length) >= (intptr_t)in) {
            out = (flip) ? end + 1 : start;
            copy_run(in, run_1_end + run_length, out);
        }
    }
    flipped[me()] = flip;
}

/**
 * @brief An implementation of standard MergeSort.
 * @note This function saves up to `n` elements after the end of the input array.
 * The sorted array is always stored from `start` to `end`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void merge_sort_write_back(T * const start, T * const end) {
    merge_sort_no_write_back(start, end);
    /* Writing back. */
    if (!flipped[me()])
        return;
    T *in = end + 1, *until = end + (end - start) + 1, *out = start;
    copy_run(in, until, out);
}

/**
 * @brief Merges two runs ranging from [`start_1`, `end_1`] and [`start_2`, `end_2`].
 * If the second run is depleted, the first one will not be flushed.
 * 
 * @param start_1 The first element of the first run.
 * @param end_1 The last element of the first run.
 * @param start_2 The first element of the second run.
 * @param end_2 The last element of the second run.
 * @param out Whither the merged runs are written.
 * Must be equal to `start_1` - (`end_2` - `start_2` + 1).
**/
static inline void merge_right_flush_only(T * const start_1, T * const end_1, T * const start_2,
        T * const end_2, T *out) {
    T *i = start_1, *j = start_2;
    T val_i = *i, val_j = *j;
    if (*end_1 <= *end_2) {
        UNROLLED_MERGER(i, end_1, if (i > end_1) { return; });
        while (true) {
            if (val_i <= val_j) {
                *out++ = val_i;
                val_i = *++i;
                if (i > end_1) {
                    return;
                }
            } else {
                *out++ = val_j;
                val_j = *++j;
            }
        }
    } else {
        UNROLLED_MERGER(j, end_2, if (j > end_2) { flush_batch(i, end_1, out); return; });
        while (true) {
            if (val_i <= val_j) {
                *out++ = val_i;
                val_i = *++i;
            } else {
                *out++ = val_j;
                val_j = *++j;
                if (j > end_2) {
                    flush_batch(i, end_1, out);
                    return;
                }
            }
        }
    }
}

/**
 * @brief An implementation of MergeSort that only uses `n`/2 additional space.
 * @note This function saves up to `n`/2 elements after the end of the input array.
 * The sorted array is always stored from `start` to `end`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void merge_sort_half_space(T * const start, T * const end) {
    /* Starting runs. */
    FORM_STARTING_RUNS_RIGHT2LEFT();

    /* Merging pairs of neighboured runs. */
    size_t const n = end - start + 1;
    for (size_t run_length = MERGE_THRESHOLD; run_length < n; run_length *= 2) {
        for (T *run_1_end = end - run_length; (intptr_t)run_1_end >= (intptr_t)start;
                run_1_end -= 2 * run_length) {
            // Copy the current run …
            T *run_1_start;  // Using a tertiary operator worsens the runtime.
            size_t run_1_length;
            if ((intptr_t)(run_1_end - run_length + 1) >= (intptr_t)start) {
                run_1_start = run_1_end - run_length + 1;
                run_1_length = run_length;
                copy_full_run(run_1_start, run_1_end, end + 1);
            } else {
                run_1_start = start;
                run_1_length = run_1_end - run_1_start + 1;
                copy_run(run_1_start, run_1_end, end + 1);
            }
            // … and merge the copy with the next run.
            merge_right_flush_only(
                end + 1,
                end + run_1_length,
                run_1_end + 1,
                run_1_end + run_length,
                run_1_start
            );
        }
    }
}

union algo_to_test __host algos[] = {
    {{ "Merge", { .wram = merge_sort_no_write_back }}},
    {{ "MergeWriteBack", { .wram = merge_sort_write_back } }},
    {{ "MergeHalfSpace", { .wram = merge_sort_half_space } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
    if (me() != 0) return EXIT_SUCCESS;

    /* Set up buffers. */
    size_t const num_of_sentinels = DMA_ALIGNED(FIRST_STEP * sizeof(T)) / sizeof(T);
    assert(2 * host_to_dpu.length + num_of_sentinels <= TRIPLE_BUFFER_LENGTH);
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
        for (size_t i = 0; i < num_of_sentinels; i++)
            buffers[me()].cache[i] = T_MIN;
    }
    T * const cache = buffers[me()].cache + num_of_sentinels;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 128;
        host_to_dpu.offset = DMA_ALIGNED(host_to_dpu.length * sizeof(T)) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }

    /* Perform test. */
    T __mram_ptr *read_from = input;
    T * const start = cache, * const end = &cache[host_to_dpu.length - 1];
    unsigned int const transfer_size = DMA_ALIGNED(sizeof(T[host_to_dpu.length]));
    sort_algo_wram * const algo = algos[host_to_dpu.algo_index].data.fct.wram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());
        mram_read_triple(read_from, cache, transfer_size);

        array_stats stats_before;
        get_stats_unsorted_wram(cache, host_to_dpu.length, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        dpu_time new_time = perfcounter_get();
        algo(start, end);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD_CYCLES;
        dpu_to_host.firsts += new_time;
        dpu_to_host.seconds += new_time * new_time;

        size_t offset = 0;  // Needed because of the MergeSort not writing back.
        if (flipped[me()]) {
            offset = host_to_dpu.length;
            cache[host_to_dpu.length - 1] = T_MIN;  // `get_stats_sorted_wram` relies on sentinels.
            flipped[me()] = false;  // Following sorting algorithms may not reset this value.
        }
        array_stats stats_after;
        get_stats_sorted_wram(cache + offset, host_to_dpu.length, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        read_from += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
