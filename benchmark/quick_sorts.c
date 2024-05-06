/**
 * @file
 * @brief Measures some implementations of QuickSort.
**/

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#include <alloc.h>
#include <defs.h>
#include <perfcounter.h>

#include "tester.h"

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
// The input length at which QuickSort changes to InsertionSort.
#define QUICK_TO_INSERTION (13)
// The call stack for iterative QuickSort.
static T **call_stack;

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
    T *curr, *i = start;
    while ((curr = i++) <= end) {
        T *prev = curr - 1;  // always valid due to the sentinel value
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

/**
 * @brief The fastest implementation of QuickSort.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort(T * const start, T * const end) {
    /* Detect base cases. */
    if (end <= start) return;
    if (end - start + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(start, end);
        return;
    }
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    quick_sort(start, i - 1);
    quick_sort(i + 1, end);
}

/**
 * @brief An implementation of QuickSort where the trivial case is not checked.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_no_triviality(T * const start, T * const end) {
    /* Detect base cases. */
    ptrdiff_t length = end - start;
    if (length + 1 <= QUICK_TO_INSERTION) {
        if (end <= start) return;
        insertion_sort_sentinel(start, end);
        return;
    }
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    quick_sort_no_triviality(start, i - 1);
    quick_sort_no_triviality(i + 1, end);
}

/**
 * @brief An implementation of QuickSort where the triviality is checked after the threshold.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_triviality_after_threshold(T * const start, T * const end) {
    /* Detect base cases. */
    if (end - start + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(start, end);
        return;
    }
    if (end <= start) return;
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    quick_sort_triviality_after_threshold(start, i - 1);
    quick_sort_triviality_after_threshold(i + 1, end);
}

/**
 * @brief An implementation of QuickSort
 * where the trivial case (partition length <= 1) is checked before recursive calls.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_check_trivial_before_call(T * const start, T * const end) {
    /* Detect base cases. */
    if (end - start + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(start, end);
        return;
    }
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    if (i - 1 > start)
        quick_sort_check_trivial_before_call(start, i - 1);
    if (end > i + 1)
        quick_sort_check_trivial_before_call(i + 1, end);
}

/**
 * @brief An implementation of QuickSort
 * which terminates if the threshold is undercut.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_no_insertion_sort(T * const start, T * const end) {
    /* Detect base cases. */
    if (end - start + 1 <= QUICK_TO_INSERTION) return;
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    quick_sort_no_insertion_sort(start, i - 1);
    quick_sort_no_insertion_sort(i + 1, end);
}

/**
 * @brief Calls a QuickSort which never calls InsertionSort.
 * Afterwards, calls InsertionSort on the whole array.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void sort_with_one_insertion_sort(T * const start, T * const end) {
    quick_sort_no_insertion_sort(start, end);
    insertion_sort_sentinel(start, end);
}

/**
 * @brief An implementation of QuickSort
 * where the threshold is checked before recursive calls.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_check_threshold_before_call(T * const start, T * const end) {
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    if ((i - 1) - start + 1 <= QUICK_TO_INSERTION)
        insertion_sort_sentinel(start, i - 1);
    else
        quick_sort_check_threshold_before_call(start, i - 1);
    if (end - (i + 1) + 1 <= QUICK_TO_INSERTION)
        insertion_sort_sentinel(i + 1, end);
    else
        quick_sort_check_threshold_before_call(i + 1, end);
}

/**
 * @brief An implementation of QuickSort
 * where the triviality and the threshold are checked before recursive calls.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_check_triviality_and_threshold_before_call(T * const start, T * const end) {
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    if ((i - 1) - start + 1 <= QUICK_TO_INSERTION) {
        if (i - 1 > start)
            insertion_sort_sentinel(start, i - 1);
    } else
        quick_sort_check_triviality_and_threshold_before_call(start, i - 1);
    if (end - (i + 1) + 1 <= QUICK_TO_INSERTION) {
        if (end > i + 1)
            insertion_sort_sentinel(start, i - 1);
    } else
        quick_sort_check_triviality_and_threshold_before_call(i + 1, end);
}

/**
 * @brief An implementation of QuickSort
 * where the triviality is checked within the threshold check.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_triviality_within_threshold(T * const start, T * const end) {
    /* Detect base cases. */
    if (end - start + 1 <= QUICK_TO_INSERTION) {
        if (end > start)
            insertion_sort_sentinel(start, end);
        return;
    }
    /* Put elements into respective partitions. */
    T * const pivot = get_pivot(start, end);
    swap(pivot, end);  // Pivot acts as sentinel value.
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *end);
        while (*--j > *end);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    quick_sort_triviality_within_threshold(start, i - 1);
    quick_sort_triviality_within_threshold(i + 1, end);
}

/**
 * @brief An implementation of QuickSort with a manual call stack.
 * To this end, enough memory should be reserved and saved in the file-wide variable `call_stack`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_iterative(T * const start, T * const end) {
    // A “call” call_stack for holding the values of `left` and `right` is maintained.
    // Since QuickSort works in-place, it is stored right after the end.
    T **start_of_call_stack = call_stack;
    *call_stack++ = start;
    *call_stack++ = end;
    do {
        T *right = *--call_stack, *left = *--call_stack;  // Pop from call stack.
        /* Detect base cases. */
        if (right - left + 1 <= QUICK_TO_INSERTION) {
            insertion_sort_sentinel(left, right);
            continue;
        }
        /* Put elements into respective partitions. */
        T * const pivot = get_pivot(left, right);  // Pivot acts as sentinel value.
        swap(pivot, right);
        T *i = left - 1, *j = right;
        while (true) {
            while (*++i < *right);
            while (*--j > *right);
            if (i >= j) break;
            swap(i, j);
        }
        swap(i, right);
        /* Put right partition on call stack. */
        if (right > i + 1) {
            *call_stack++ = i + 1;
            *call_stack++ = right;
        }
        /* Put left partition on call stack. */
        if (i - 1 > left) {
            *call_stack++ = left;
            *call_stack++ = i - 1;
        }
    } while (call_stack != start_of_call_stack);
}

int main() {
    triple_buffers buffers;
    allocate_triple_buffer(&buffers);
    if (me() != 0) return EXIT_SUCCESS;
    if (DPU_INPUT_ARGUMENTS.mode == 0) {  // called via debugger?
        DPU_INPUT_ARGUMENTS.mode = 2;
        DPU_INPUT_ARGUMENTS.n_reps = 10;
        DPU_INPUT_ARGUMENTS.upper_bound = 4;
    }
    perfcounter_config(COUNT_CYCLES, false);

    char name[] = "BASE SORTING ALGORITHMS";
    struct algo_to_test const algos[] = {
        { quick_sort, "Normal" },
        { quick_sort_check_trivial_before_call, "TrivialBC" },
        { quick_sort_no_triviality, "NoTrivial" },
        { sort_with_one_insertion_sort, "OneInsertion" },
        { quick_sort_check_threshold_before_call, "ThreshBC" },
        { quick_sort_check_triviality_and_threshold_before_call, "ThreshTrivBC" },
        { quick_sort_triviality_after_threshold, "ThreshThenTriv" },
        { quick_sort_triviality_within_threshold, "TrivInThresh" },
        { quick_sort_iterative, "QuickIt" },
    };
    size_t lengths[] = { 20, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];
    assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    /* Reserve memory for custom call stack, which is needed by the iterative QuickSort. */
    // 20 pointers on the stack was the most I’ve seen for 1024 elements
    // so the space reserved here should be enough.
    size_t const log = 31 - __builtin_clz(lengths[num_of_lengths - 1]);
    call_stack = mem_alloc(4 * log * sizeof(T *));

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, &buffers, &DPU_INPUT_ARGUMENTS);
    return EXIT_SUCCESS;
}
