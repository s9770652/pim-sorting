/**
 * @file
 * @brief Sequential sorting of WRAM data through the fastest QuickSort.
**/

#ifndef _BASE_SORT_H_
#define _BASE_SORT_H_

#include <stdbool.h>
#include "common.h"

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
static void insertion_sort_wram(T * const start, T * const end) {
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

#if (STABLE)

#define __MERGE_THRESHOLD__ (14)

/**
 * @brief Creating the starting runs for MergeSort.
 * The runs are formed from right to left, such that the first run may be smaller.
**/
#define FORM_STARTING_RUNS_RIGHT2LEFT()                                                \
for (T *t = end; t > start; t -= __MERGE_THRESHOLD__) {                                \
    T *t_ = t - __MERGE_THRESHOLD__ + 1 > start ? t - __MERGE_THRESHOLD__ + 1 : start; \
    T before_sentinel = *(t_ - 1);                                                     \
    *(t_ - 1) = T_MIN;                                                                 \
    insertion_sort_wram(t_, t);                                                        \
    *(t_ - 1) = before_sentinel;                                                       \
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using batches of length `24`:
 * If there are at least `24` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `24`th in good cases.
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void flush_batch_wram(T *in, T *until, T *out) {
    while (in + 24 - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < 24; k++)
            *(out + k) = *(in + k);
        out += 24;
        in += 24;
    }
    while (in <= until) {
        *out++ = *in++;
    }
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using batches of length `__MERGE_THRESHOLD__`:
 * If there are at least `__MERGE_THRESHOLD__` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `__MERGE_THRESHOLD__`th in good cases.
 * @sa copy_full_run_wram
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void copy_run_wram(T *in, T *until, T *out) {
    while (in + __MERGE_THRESHOLD__ - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < __MERGE_THRESHOLD__; k++)
            *(out + k) = *(in + k);
        out += __MERGE_THRESHOLD__;
        in += __MERGE_THRESHOLD__;
    }
    while (in <= until) {
        *out++ = *in++;
    }
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using *only* batches of length `__MERGE_THRESHOLD__`:
 * If there are at least `__MERGE_THRESHOLD__` many elements to copy, they are copied at once.
 * For this reason, the length of the buffer must be a multiple of `__MERGE_THRESHOLD__`.
 * @sa copy_run
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void copy_full_run_wram(T *in, T *until, T *out) {
    while (in + __MERGE_THRESHOLD__ - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < __MERGE_THRESHOLD__; k++)
            *(out + k) = *(in + k);
        out += __MERGE_THRESHOLD__;
        in += __MERGE_THRESHOLD__;
    }
}

/**
 * @brief How many iterations in the mergers are unrolled.
 * @note This value is capped at 16, as otherwise the IRAM is in danger of overflowing.
**/
#define UNROLL_FACTOR_WRAM (MIN(__MERGE_THRESHOLD__, 16))

/**
 * @brief Merges the two runs within the merger functions in unrolled loops.
 * 
 * @param ptr The pointer of the run whose last element is less than that of the other run.
 * @param end The address of said last element.
 * @param on_depletion An if-block for when and what to do if said run is fully merged.
**/
#define UNROLLED_MERGER_WRAM(ptr, end, on_depletion)      \
while (ptr <= end - UNROLL_FACTOR_WRAM + 1) {             \
    _Pragma("unroll")                                     \
    for (size_t k = 0; k < UNROLL_FACTOR_WRAM; k++) {     \
        if (val_i <= val_j) {                             \
            *(out + k) = val_i;                           \
            val_i = *++i;                                 \
        } else {                                          \
            *(out + k) = val_j;                           \
            val_j = *++j;                                 \
        }                                                 \
    }                                                     \
    out += UNROLL_FACTOR_WRAM;                            \
};                                                        \
on_depletion                                              \
while (ptr <= end - (UNROLL_FACTOR_WRAM / 2) + 1) {       \
    _Pragma("unroll")                                     \
    for (size_t k = 0; k < UNROLL_FACTOR_WRAM / 2; k++) { \
        if (val_i <= val_j) {                             \
            *(out + k) = val_i;                           \
            val_i = *++i;                                 \
        } else {                                          \
            *(out + k) = val_j;                           \
            val_j = *++j;                                 \
        }                                                 \
    }                                                     \
    out += UNROLL_FACTOR_WRAM / 2;                        \
}                                                         \
on_depletion

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
static inline void merge_right_flush_only_wram(T * const start_1, T * const end_1,
        T * const start_2, T * const end_2, T *out) {
    T *i = start_1, *j = start_2;
    T val_i = *i, val_j = *j;
    if (*end_1 <= *end_2) {
        UNROLLED_MERGER_WRAM(i, end_1, if (i > end_1) { return; });
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
        UNROLLED_MERGER_WRAM(j, end_2, if (j > end_2) { flush_batch_wram(i, end_1, out); return; });
        while (true) {
            if (val_i <= val_j) {
                *out++ = val_i;
                val_i = *++i;
            } else {
                *out++ = val_j;
                val_j = *++j;
                if (j > end_2) {
                    flush_batch_wram(i, end_1, out);
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
static void wram_sort(T * const start, T * const end) {
    /* Starting runs. */
    FORM_STARTING_RUNS_RIGHT2LEFT();

    /* Merging pairs of neighboured runs. */
    size_t const n = end - start + 1;
    for (size_t run_length = __MERGE_THRESHOLD__; run_length < n; run_length *= 2) {
        for (T *run_1_end = end - run_length; (intptr_t)run_1_end >= (intptr_t)start;
                run_1_end -= 2 * run_length) {
            // Copy the current run …
            T *run_1_start;  // Using a tertiary operator worsens the runtime.
            size_t run_1_length;
            if ((intptr_t)(run_1_end - run_length + 1) >= (intptr_t)start) {
                run_1_start = run_1_end - run_length + 1;
                run_1_length = run_length;
                copy_full_run_wram(run_1_start, run_1_end, end + 1);
            } else {
                run_1_start = start;
                run_1_length = run_1_end - run_1_start + 1;
                copy_run_wram(run_1_start, run_1_end, end + 1);
            }
            // … and merge the copy with the next run.
            merge_right_flush_only_wram(
                end + 1,
                end + run_1_length,
                run_1_end + 1,
                run_1_end + run_length,
                run_1_start
            );
        }
    }
}

#else  // STABLE

#include "pivot.h"

#define __QUICK_THRESHOLD__ (18)

/* Defining building blocks for QuickSort, which remain the same. */
// The main body of QuickSort remains the same no matter the implementation variant.
#define QUICK_BODY()                                         \
T * const pivot = get_pivot(left, right);                    \
T const pivot_value = *pivot;                                \
swap(pivot, right);  /* The pivot acts as sentinel value. */ \
T *i = left - 1, *j = right;                                 \
while (true) {                                               \
    while (*++i < pivot_value);                              \
    while (*--j > pivot_value);                              \
    if (i >= j) break;                                       \
    swap(i, j);                                              \
}                                                            \
swap(i, right)

#define QUICK_GET_SHORTER_PARTITION() (!__RECURSIVE__)

// Whether the partition has a length below the threshold.
#define QUICK_IS_THRESHOLD_UNDERCUT() (right - left + 1 <= __QUICK_THRESHOLD__)

#define QUICK_FALLBACK() insertion_sort_wram(left, right)

#define QUICK_IS_TRIVIAL_LEFT() (i - 1 <= left)
#define QUICK_IS_TRIVIAL_RIGHT() (right <= i + 1)

#ifdef UINT64  // recursive variant

#define __RECURSIVE__ (true)

// Sets up `left` and `right` as synonyms for `start` and `end`.
// They are distinct only in the iterative variant.
#define QUICK_HEAD() T *left = start, *right = end

// Unneeded for the recursive variant.
#define QUICK_TAIL()

// Obviously, ending the current QuickSort is done via `return`.
#define QUICK_STOP() return

#define QUICK_CALL_LEFT() quick_sort_wram(left, i - 1)
#define QUICK_CALL_RIGHT() quick_sort_wram(i + 1, right)

#else  // UINT64

#define __RECURSIVE__ (false)

static T *call_stacks[NR_TASKLETS][40];  // call stack for iter. QuickSort

// A “call” stack for holding the values of `left` and `right` is maintained.
// Its memory must be reserved beforehand.
#define QUICK_HEAD()                                    \
T ** const start_of_call_stack = &call_stacks[me()][0]; \
T **call_stack = start_of_call_stack;                   \
*call_stack++ = start;                                  \
*call_stack++ = end;                                    \
do {                                                    \
    T *right = *--call_stack, *left = *--call_stack

// Closing the loop which pops from the stack.
#define QUICK_TAIL() } while (call_stack != start_of_call_stack)

// Obviously, ending the current QuickSort is done via `continue`.
#define QUICK_STOP() continue

#define QUICK_CALL_LEFT(name) do { *call_stack++ = left; *call_stack++ = i - 1; } while (false)
#define QUICK_CALL_RIGHT(name) do { *call_stack++ = i + 1; *call_stack++ = right; } while (false)

#endif  // UINT64

/**
 * @brief A fast implementation of QuickSort
 * based on `quick_sort_check_trivial_before_call` from `benchmark/quick_sorts.c`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 */
static void wram_sort(T * const start, T * const end) {
    QUICK_HEAD();
    if (QUICK_IS_THRESHOLD_UNDERCUT()) {
        QUICK_FALLBACK();
        QUICK_STOP();
    }
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        if (!QUICK_IS_TRIVIAL_LEFT())
            QUICK_CALL_LEFT();
        if (!QUICK_IS_TRIVIAL_RIGHT())
            QUICK_CALL_RIGHT();
    } else {
        if (!QUICK_IS_TRIVIAL_RIGHT())
            QUICK_CALL_RIGHT();
        if (!QUICK_IS_TRIVIAL_LEFT())
            QUICK_CALL_LEFT();
    }
    QUICK_TAIL();
}

#undef QUICK_BODY
#undef QUICK_GET_SHORTER_PARTITION
#undef QUICK_IS_THRESHOLD_UNDERCUT
#undef QUICK_FALLBACK
#undef QUICK_IS_TRIVIAL_LEFT
#undef QUICK_IS_TRIVIAL_RIGHT
#undef __RECURSIVE__
#undef QUICK_HEAD
#undef QUICK_TAIL
#undef QUICK_STOP
#undef QUICK_CALL_LEFT
#undef QUICK_CALL_RIGHT

#endif  // STABLE

#endif  // _BASE_SORT_H_
