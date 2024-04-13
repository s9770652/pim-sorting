#include <assert.h>
#include <stdbool.h>

#include <alloc.h>
#include <defs.h>

#include "base_sorts.h"
#include "tester.h"

// The input length at which QuickSort changes to InsertionSort.
#define QUICK_TO_INSERTION (96)
// The call stack for iterative QuickSort.
T **call_stack;

/**
 * @brief An implementation of standard InsertionSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void insertion_sort_nosentinel(T * const start, T * const end) {
    T *curr, *i = start;
    while ((curr = i++) <= end) {  // todo: `++i` is slower‽
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
    while ((curr = i++) <= end) {  // todo: `++i` is slower‽
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value // todo: actually not because of i++ instead of ++i
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of InsertionSort where compared elements need not be neighbours.
 * Needed by the classic ShellSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_steps(T * const start, T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) <= end) {
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
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_large_steps(T * const start, T * const end, size_t const step) {
    T *curr, *i = start;
    while ((curr = (i += step)) <= end) {
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
 * @brief An implementation of standard ShellSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void shell_sort_classic(T * const start, T * const end) {
    // Sort all elements which are n/2, n/4, …, 4, 2 indices apart.
    size_t step = (end - start + 1) / 2;
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
    assert((uintptr_t)start >= (steps[0] << DIV));
    size_t i = 0;
    for (; i < 2; i++)
        for (size_t j = 0; j < steps[i]; j++)
            insertion_sort_with_steps(&start[j], end, steps[i]);
    for (; i < sizeof steps / sizeof steps[0]; i++)
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
 * Used by QuickSort.
 * Currently, the method of choosing must be changed by (un-)commenting the respective code lines.
 * Possible are:
 * - always the leftmost element
 * - the mean of the leftmost and the rightmost element
 * - the median of the leftmost, middle and rightmost element
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 *
 * @return The pivot element.
**/
static inline T get_pivot(T const * const start, T const * const end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
    /* Always the leftmost element. */
    // return *start;
    /* The mean of the leftmost and the rightmost element. */
    return (*start + *end) / 2;  // todo: source of error for large numbers!
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
    if (end <= start) return;
    if (end - start + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(start, end);
        return;
    };
    /* Put elements into respective partitions. */
    T * const pivot = end;
    swap(pivot, end);
    T *i = start - 1, *j = end;
    while (true) {
        while (*++i < *pivot);
        while (*--j > *pivot);
        if (i >= j) break;
        swap(i, j);
    }
    swap(i, end);
    /* Sort left and right partitions. */
    quick_sort_recursive(start, i - 1);
    quick_sort_recursive(i + 1, end);
}

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
        T * const pivot = right;
        swap(pivot, right);
        T *i = left - 1, *j = right;
        while (true) {
            while (*++i < *pivot);
            while (*--j > *pivot);
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

void test_wram_sorts(triple_buffers * const buffers, struct dpu_arguments * const args) {
    if (me() != 0) return;

    char name[] = "BASE SORTING ALGORITHMS";
    struct algos_to_test const algos[] = {
        // { insertion_sort_nosentinel, "Insert" },
        // { insertion_sort_sentinel, "Insert (Sent.)" },
        { shell_sort_classic, "Shell" },
        { shell_sort_ciura, "Shell (Ciura)" },
        // { shell_sort_custom_step_6, "Shell (Custom)" },  // todo: Add skip over step
        { quick_sort_recursive, "Quick (Rec.)" },
        { quick_sort_iterative, "Quick (It.)" },
    };
    size_t lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024 };  // todo: add missing sizes
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];
    assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    /* Reserve memory for custom call stack, which is needed by the iterative QuickSort. */
    size_t const log = 31 - __builtin_clz(lengths[num_of_lengths - 1]);
    call_stack = mem_alloc(4 * log * sizeof(T *));  // Should be enough as 20 pointers was the most I've seen.

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
    size_t sentinels = (BIG_STEP > 9) ? BIG_STEP : num_of_algos;
    assert(lengths[num_of_lengths - 1] + sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
    for (size_t i = 0; i < sentinels; i++)
        buffers->cache[i] = T_MIN;
    buffers->cache += sentinels;

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, buffers, args);
}
