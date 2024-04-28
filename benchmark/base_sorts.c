#include <assert.h>
#include <stdbool.h>

#include <alloc.h>
#include <defs.h>

#include "base_sorts.h"
#include "tester.h"

// The input length at which QuickSort changes to InsertionSort.
#define QUICK_TO_INSERTION (12)
// The input length at which HeapSort changes to InsertionSort.
#define HEAP_TO_INSERTION (12)
// The call stack for iterative QuickSort.
T **call_stack;

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

#define BIG_STEP (-1)

/**
 * @brief Creates a ShellSort of the name `shell_sort_custom_step_x`
 * with x being the step size before the final InsertionSort.
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
        _Pragma("nounroll")                                                 \
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

/**
 * @brief Returns a pivot element for a WRAM array.
 * Used by QuickSort.
 * Currently, the method of choosing must be changed by (un-)commenting the respective code lines.
 * Possible are:
 * - always the rightmost element
 * - the median of the leftmost, middle and rightmost element
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 *
 * @return The pivot element.
**/
static inline T *get_pivot(T const * const start, T const * const end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
    /* Always the rightmost element. */
    return (T *)end;
    // /* The median of the leftmost, middle and rightmost element. */
    // T *middle = (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
    // if ((*start > *middle) ^ (*start > *end))
    //     return (T *)start;
    // else if ((*start > *middle) ^ (*end > *middle))
    //     return (T *)middle;
    // else
    //     return (T *)end;
}

/**
 * @brief An implementation of standard QuickSort.
 * @internal Again, the compiler is iffy.
 * Detecting the base cases before doing a recursive calls worsens the performance.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_recursive(T * const start, T * const end) {
    /* Detect base cases. */
    if (end <= start) return;
    if (end - start + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(start, end);
        return;
    };
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
    quick_sort_recursive(start, i - 1);
    quick_sort_recursive(i + 1, end);
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

/**
 * @brief Sifts a value down in a binary max-heap.
 * 
 * @param array A binary tree whose left and right subtrees are heapified.
 * @param n The size of the binary tree.
 * @param root The index of the value to sift down.
**/
static void heapify(T heap[], size_t const n, size_t const root) {
    T const root_value = heap[root];
    size_t father = root, son;
    while ((son = father * 2 + 1) < n) {  // left son
        if ((son + 1 < n) && (heap[son + 1] > heap[son]))  // Check if right son is bigger.
            son++;
        if (heap[son] <= root_value)  // Stop if both sons are smaller than their father.
            break;
        heap[father] = heap[son];  // Shift son up.
        father = son;
    }
    heap[father] = root_value;
}

/**
 * @brief An implementation of standard HeapSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void heap_sort(T * const start, T * const end) {
    size_t n = end - start + 1;
    /* Build a heap using Floyd’s method. */
    for (size_t r = n / 2; r > 0; r--) {
        heapify(start, n, r - 1);
    }
    /* Sort by repeatedly putting the root at the end of the heap. */
    size_t i;
    for (i = n - 1; i > HEAP_TO_INSERTION; i--) {
        swap(&start[0], &start[i]);
        heapify(start, i, 0);
    }
    insertion_sort_sentinel(start, &start[i]);
}

void test_wram_sorts(triple_buffers * const buffers, struct dpu_arguments * const args) {
    if (me() != 0) return;

    char name[] = "BASE SORTING ALGORITHMS";
    struct algo_to_test const algos[] = {
        { insertion_sort_nosentinel, "Insert" },
        { insertion_sort_sentinel, "InsertSent" },
        { shell_sort_ciura, "ShellCiura" },
        { quick_sort_recursive, "QuickRec" },
        { quick_sort_iterative, "QuickIt" },
        { heap_sort, "Heap" },
    };
    // size_t lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 384, 512, 768, 1024, 1280 };
    // size_t lengths[] = { 24, 32, 48, 64, 96, 128 };
    size_t lengths[] = {
        24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80,
        84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128
    };
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];
    assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    /* Add additional sentinel values. */
    size_t num_of_sentinels = 17;
    assert(lengths[num_of_lengths - 1] + num_of_sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
    for (size_t i = 0; i < num_of_sentinels; i++)
        buffers->cache[i] = T_MIN;
    buffers->cache += num_of_sentinels;

    /* Reserve memory for custom call stack, which is needed by the iterative QuickSort. */
    // 20 pointers on the stack was the most I’ve seen for 1024 elements
    // so the space reserved here should be enough.
    size_t const log = 31 - __builtin_clz(lengths[num_of_lengths - 1]);
    call_stack = mem_alloc(4 * log * sizeof(T *));

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, buffers, args);
}

void test_very_small_sorts(triple_buffers * const buffers, struct dpu_arguments * const args) {
    if (me() != 0) return;

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
        buffers->cache[i] = T_MIN;
    buffers->cache += num_of_sentinels;

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, buffers, args);
}
