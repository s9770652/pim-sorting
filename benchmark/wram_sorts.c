/**
 * @file
 * @brief Measures sorting algorithms used on full WRAM.
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <alloc.h>
#include <defs.h>
#include <perfcounter.h>

#include "tester.h"

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
// The input length at which QuickSort changes to InsertionSort.
#define QUICK_TO_INSERTION (16)
// The input length at which HeapSort changes to InsertionSort.
#define HEAP_TO_INSERTION (12)
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
        { quick_sort_recursive, "QuickRec" },
        { quick_sort_iterative, "QuickIt" },
        { heap_sort, "Heap" },
    };
    size_t lengths[] = { 20, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
    // size_t lengths[] = { 24, 32, 48, 64, 96, 128 };
    // size_t lengths[] = {
    //     16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80,
    //     84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128
    // };
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];
    assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    /* Add additional sentinel values. */
    size_t num_of_sentinels = 17;
    assert(lengths[num_of_lengths - 1] + num_of_sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
    for (size_t i = 0; i < num_of_sentinels; i++)
        buffers.cache[i] = T_MIN;
    buffers.cache += num_of_sentinels;

    /* Reserve memory for custom call stack, which is needed by the iterative QuickSort. */
    // 20 pointers on the stack was the most I’ve seen for 1024 elements
    // so the space reserved here should be enough.
    size_t const log = 31 - __builtin_clz(lengths[num_of_lengths - 1]);
    call_stack = mem_alloc(4 * log * sizeof(T *));

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, &buffers, &DPU_INPUT_ARGUMENTS);
    return EXIT_SUCCESS;
}
