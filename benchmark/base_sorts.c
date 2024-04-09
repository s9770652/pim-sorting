#include <assert.h>

#include <defs.h>

#include "base_sorts.h"
#include "tester.h"

// the input length at which QuickSort changes to InsertionSort
#define QUICK_TO_INSERTION (96)

/**
 * @brief An implementation of standard InsertionSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
**/
static void insertion_sort_nosentinel(T * const start, T * const end) {
    T *curr, *i = start;
    while ((curr = i++) < end) {  // todo: `++i` is slower‽
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
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
**/
static void insertion_sort_sentinel(T * const start, T * const end) {
    T *curr, *i = start;
    while ((curr = i++) < end) {  // todo: `++i` is slower‽
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of InsertionSort where compared elements need not be neighbours.
 * Needed by the classic ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_steps(T * const start, T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) < end) {
        T *prev = curr - step;
        T const to_sort = *curr;
        while (prev >= start && *prev > to_sort) {
            *curr = *prev;
            curr = prev;
            prev -= step;
        }
        *curr = to_sort;
    }
}

/**
 * @brief The same as `insertion_sort_with_steps`
 * but additionally checks whether `prev` wrapped around.
 * Needed by the classic ShellSort.
 *
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_large_steps(T * const start, T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) < end) {
        T *prev = curr - step;
        T const to_sort = *curr;
        while (prev >= start && *prev > to_sort) {
            *curr = *prev;
            curr = prev;
            prev -= step;
            if (prev >= end) break;  // Since `end` – `start` is small, this check is sufficient.
        }
        *curr = to_sort;
    }
}

/**
 * @brief A combination of `insertion_sort_sentinel` and `insertion_sort_with_steps`.
 * Needed by ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_steps_sentinel(T * const start, T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) < end) {
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
 * @brief An implementation of standard ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
**/
static void shell_sort_classic(T * const start, T * const end) {
    // Sort all elements which are n/2, n/4, …, 4, 2 indices apart.
    size_t step = (end - start) / 2;
    for (; step >= ((uintptr_t)start >> DIV); step /= 2)  // Likely: step ≥ 512
        for (size_t j = 0; j < step; j++)
            insertion_sort_with_large_steps(&start[j], end, step);
    for (; step >= 2; step /= 2)  // Likely: step ≤ 256
        for (size_t j = 0; j < step; j++)
            insertion_sort_with_steps(&start[j], end, step);
    // Do one final sort with a step size of 1.
    insertion_sort_sentinel(start, end);
}

// Ciura
static void shell_sort_ciura(T * const start, T * const end) {
    size_t const steps[] = { 57, 23, 10, 4 };
    for (size_t i = 0; i < 2; i++)
        for (size_t j = 0; j < steps[i]; j++)
            insertion_sort_with_steps(&start[j], end, steps[i]);
    for (size_t i = 2; i < 4; i++)
        for (size_t j = 0; j < steps[i]; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, steps[i]);
    // Do one final sort with a step size of 1.
    insertion_sort_sentinel(start, end);
}

#define SHELL_SORT_CUSTOM_STEP_X(step)                                      \
static void shell_sort_custom_step_##step(T * const start, T * const end) { \
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

// Ciura-like
// static void shell_sort_custom(T * const start, T * const end) {
//     size_t const step = 6;
//     if (end - start > 16)
//         for (size_t j = 0; j < step; j++)
//             insertion_sort_with_steps_sentinel(&start[j], end, step);
//     // Do one final sort with a step size of 1.
//     insertion_sort_sentinel(start, end);
// }

/**
 * @brief Swaps the content of two addresses.
 * 
 * @param a First WRAM address.
 * @param b Second WRAM address.
**/
static void swap(T * const a, T * const b) {
    T const temp = *a;
    *a = *b;
    *b = temp;
}

/**
 * @brief Returns a pivot element for a WRAM array.
 * The method of choosing currently must be changed by (un-)commenting the respective code lines.
 * Possible are:
 * - always the leftmost element
 * - the mean of the leftmost and the rightmost element
 * - the median of the leftmost, middle and rightmost element
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 *
 * @return The pivot element.
**/
static inline T get_pivot(T const * const start, T const * const end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
    /* Always the leftmost element. */
    // return *start;
    /* The mean of the leftmost and the rightmost element. */
    return (*start + *(end-1)) / 2;
    /* The median of the leftmost, middle and rightmost element. */
    // T *middle = (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
    // if ((*start > *middle) ^ (*start > *end))
    //     return *start;
    // else if ((*middle < *start) ^ (*middle < *end))
    //     return *middle;
    // else
    //     return *end;
}

static void quick_sort_recursive(T * const start, T * const end) {
    /* Detect base cases. */
    if (end - start <= QUICK_TO_INSERTION) {  // false if `end < start` due to wrapping
        insertion_sort_sentinel(start, end);
        return;
    } else if (end <= start) return;
    /* Put elements into respective partitions. */
    T *i = start, *j = end;
    T pivot = get_pivot(start, end);
    do {
        while (*i < pivot) i++;
        while (*j > pivot) j--;

        if (i <= j)
            swap(i++, j--);
    } while (i <= j);
    /* Sort left and right partitions. */
    quick_sort_recursive(start, j);
    quick_sort_recursive(i, end);
}

static void quick_sort_iterative(T * const start, T * const end) {
    // A “call” stack for holding the values of `left` and `right` is maintained.
    // Since QuickSort works in-place, it is stored right after the end.
    T **stack = (T **)(uintptr_t)end;
    *++stack = start;
    *++stack = end;
    do {
        T *right = *stack--, *left = *stack--;  // Pop from stack.
        /* Detect base cases. */
        if (right - left <= QUICK_TO_INSERTION) {  // false if `right < left` due to wrapping
            insertion_sort_sentinel(left, right);
            continue;
        } else if (right <= left) return;
        /* Put elements into respective partitions. */
        T *i = left, *j = right;
        T pivot = get_pivot(left, right);
        do {
            while (*i < pivot) i++;
            while (*j > pivot) j--;

            if (i <= j)
                swap(i++, j--);
        } while (i <= j);
        /* Put left partition on stack. */
        if (j > left) {
            *++stack = left;
            *++stack = j;
        }
        /* Put right partition on stack. */
        if (right > i) {
            *++stack = i;
            *++stack = right;
        }
    } while (stack != (T **)(uintptr_t)end);
}

void test_wram_sorts(triple_buffers * const buffers, struct dpu_arguments * const args) {
    if (me() != 0) return;

    char name[] = "BASE SORTING ALGORITHMS";
    struct algos_to_test const algos[] = {
        // { insertion_sort_nosentinel, "Insert" },
        { insertion_sort_sentinel, "Insert (Sent.)" },
        // { shell_sort_classic, "Shell" },
        // { shell_sort_ciura, "Shell (Ciura)" },
        // { shell_sort_custom_step_6, "Shell (Custom)" },  // todo: Add skip over step
        // { quick_sort_recursive, "Quick (Rec.)" },
        // { quick_sort_iterative, "Quick (It.)" }
    };
    size_t lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024 };  // todo: add missing sizes
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];
    assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, buffers, args);
}

void test_custom_shell_sorts(triple_buffers * const buffers, struct dpu_arguments * const args) {
    if (me() != 0) return;

    char name[] = "CUSTOM SHELLSORTS";
    struct algos_to_test algos[] = {
        { insertion_sort_sentinel, "1" },
        { shell_sort_custom_step_2, "2" },
        { shell_sort_custom_step_3, "3" },
        { shell_sort_custom_step_4, "4" },
        { shell_sort_custom_step_5, "5" },
        { shell_sort_custom_step_6, "6" },
        { shell_sort_custom_step_7, "7" },
        { shell_sort_custom_step_8, "8" },
        { shell_sort_custom_step_9, "9" },
    };
    size_t lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128 };
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];

    /* Add additional sentinel values. */
    assert(lengths[num_of_lengths - 1] + num_of_algos <= (TRIPLE_BUFFER_SIZE >> DIV));
    for (size_t i = 0; i < num_of_algos; i++)
        buffers->cache[i] = T_MIN;
    buffers->cache += num_of_algos;

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, buffers, args);
}
