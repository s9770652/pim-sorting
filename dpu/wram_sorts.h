/**
 * @file
 * @brief An implementation of the fastest WRAM QuickSort.
**/

#ifndef _BASE_SORT_H_
#define _BASE_SORT_H_

#include <stdbool.h>

#include "common.h"
#include "pivot.h"

static __attribute__((unused)) T *call_stacks[NR_TASKLETS][40];  // call stack for iter. QuickSort

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
#define QUICK_IS_THRESHOLD_UNDERCUT() (right - left + 1 <= QUICK_THRESHOLD)

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
static void quick_sort_wram(T * const start, T * const end) {
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

#endif  // _BASE_SORT_H_
