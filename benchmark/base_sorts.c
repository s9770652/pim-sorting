/**
 * @file
 * @brief Measures sorting algorithms intended for small input arrays.
 * 
 * The tested algorithms are:
 * - InsertionSort
 * - ShellSort
 * - BubbleSort
 * - SelectionSort
 * Whether the ShellSorts have two rounds or three, depend on the value of `BIG_STEP`.
 * See their respective documentation for more information.
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <alloc.h>
#include <defs.h>
#include <perfcounter.h>

#include "random_generator.h"

#include "tester.h"

#define BIG_STEP (-1)

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;

/**
 * @brief An implementation of standard BubbleSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void bubble_sort_nonadaptive(T * const start, T * const end) {
    for (T *until = end; until > start; until--) {
        for (T *i = start; i < until; i++) {
            if (*i > *(i+1)) {
                swap(i, i+1);
            }
        }
    }
}

/**
 * @brief An implementation of BubbleSort which terminates early if the array is sorted.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void bubble_sort_adaptive(T * const start, T * const end) {
    bool swapped;
    T *until = end;
    do {
        swapped = false;
        for (T *i = start; i < until; i++) {
            if (*i > *(i+1)) {
                swap(i, i+1);
                swapped = true;
            }
        }
        until--;
    } while (swapped);
}

/**
 * @brief An implementation of standard SelectionSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
void selection_sort(T * const start, T * const end) {
    for (T *i = start; i < end; i++) {
        T *min = i;
        for (T *j = i + 1; j <= end; j++) {
            if (*j < *min) {
                min = j;
            }
        }
        swap(i, min);
    }
}

/**
 * @brief An implementation of standard InsertionSort.
 * @internal The compiler is iffy when it comes to this function.
 * Using `curr = ++i` would algorithmically make sense as a list of length 1 is always sorted,
 * yet it actually runs ~2% slower.
 * And since moves and additions cost the same, the it does also does not benefit
 * from being able to use `prev = i` instead of `prev = curr - 1`.
 * The same slowdowns also happens when using `*i = start + 1`.
 * Since only a few extra instructions are performed each call,
 * I refrain from injecting Assembly code for the sake of maintainability and readability
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void insertion_sort_nosentinel(T * const start, T * const end) {
    T *curr, *i = start;
    while ((curr = i++) <= end) {
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (prev >= start && *prev > to_sort) {
            *curr = *prev;
            curr = prev--;
        }
        *curr = to_sort;
    }
}

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
 * @brief A combination of `insertion_sort_sentinel` and `insertion_sort_with_steps`.
 * Needed by ShellSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_steps_sentinel(T * const start, T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) <= end) {
        T *prev = curr - step;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev;
            prev -= step;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of ShellSort using Ciura’s optimal sequence for 128 elements.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void shell_sort_ciura(T * const start, T * const end) {
    if (end - start + 1 <= 64) {
        for (size_t j = 0; j < 6; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, 6);
    } else {
        for (size_t j = 0; j < 17; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, 17);
        for (size_t j = 0; j < 4; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, 4);
    }
    insertion_sort_sentinel(start, end);
}

/**
 * @brief Creates a ShellSort of the name `shell_sort_custom_step_x`
 * with x ∈ {2, …, 9} being the step size before the final InsertionSort.
 * If `BIG_STEP` is bigger than `x`, there will be three rounds
 * with `BIG_STEP` being the first step size.
 * 
 * @param step The step size before the final InsertionSort. [Parameter of the macro]
 * @param start The first element of the WRAM array to sort. [Parameter of the ShellSort]
 * @param end The last element of said array. [Parameter of the ShellSort]
**/
#define SHELL_SORT_CUSTOM_STEP_X(step)                                      \
static void shell_sort_custom_step_##step(T * const start, T * const end) { \
    if (BIG_STEP >= step) {                                                 \
        _Pragma("nounroll")  /* The .text region may overflow otherwise. */ \
        for (size_t j = 0; j < BIG_STEP; j++)                               \
            insertion_sort_with_steps_sentinel(&start[j], end, BIG_STEP);   \
    }                                                                       \
    for (size_t j = 0; j < step; j++)                                       \
        insertion_sort_with_steps_sentinel(&start[j], end, step);           \
    insertion_sort_sentinel(start, end);                                    \
}

SHELL_SORT_CUSTOM_STEP_X(2)
SHELL_SORT_CUSTOM_STEP_X(3)
SHELL_SORT_CUSTOM_STEP_X(4)
SHELL_SORT_CUSTOM_STEP_X(5)
SHELL_SORT_CUSTOM_STEP_X(6)
SHELL_SORT_CUSTOM_STEP_X(7)
SHELL_SORT_CUSTOM_STEP_X(8)
SHELL_SORT_CUSTOM_STEP_X(9)

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

    char name[] = "SMALL SORTING ALGORITHMS";
    struct algo_to_test algos[] = {
        { insertion_sort_sentinel, "1" },
        { shell_sort_custom_step_2, "2" },
        { shell_sort_custom_step_3, "3" },
        { shell_sort_custom_step_4, "4" },
        { shell_sort_custom_step_5, "5" },
        { shell_sort_custom_step_6, "6" },
        { shell_sort_custom_step_7, "7" },
        { shell_sort_custom_step_8, "8" },
        { shell_sort_custom_step_9, "9" },
        { shell_sort_ciura, "Ciura" },
        { insertion_sort_nosentinel, "1NoSentinel" },
        { bubble_sort_adaptive, "BubbleAdapt" },
        { bubble_sort_nonadaptive, "BubbleNonAdapt" },
        { selection_sort, "Selection" },
    };
    size_t lengths[] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24
    };
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];

    /* Add additional sentinel values. */
    size_t num_of_sentinels = 9;
    assert(lengths[num_of_lengths - 1] + num_of_sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
    for (size_t i = 0; i < num_of_sentinels; i++)
        buffers.cache[i] = T_MIN;
    buffers.cache += num_of_sentinels;

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, &buffers, &DPU_INPUT_ARGUMENTS);
    return EXIT_SUCCESS;
}
