/**
 * @file
 * @brief Measures runtimes of some implementations of QuickSort.
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

/* Defining building blocks for QuickSort, which remain the same. */
#define RECURSIVE (false)

// The main body of QuickSort remains the same no matter the implementation variant.
#define QUICK_BODY()                                     \
T * const pivot = get_pivot(left, right);                \
swap(pivot, right);  /* Pivot acts as sentinel value. */ \
T *i = left - 1, *j = right;                             \
while (true) {                                           \
    while (*++i < *right);                               \
    while (*--j > *right);                               \
    if (i >= j) break;                                   \
    swap(i, j);                                          \
}                                                        \
swap(i, right)

#if RECURSIVE  // recursive variant

// Sets up `left` and `right` as synonyms for `start` and `end`.
// They are distinct only in the iterative variant.
#define QUICK_HEAD() left = start; right = end

// Performs a recursive call.
#define QUICK_CALL(name, l, r) name(l, r)

// Unneeded for the recursive variant.
#define QUICK_TAIL()

// Obviously, ending the current QuickSort is done via `return`.
#define QUICK_STOP() return

#else  // iterative variant

// A “call” stack for holding the values of `left` and `right` is maintained.
// Its memory must be reserved beforehand.
#define QUICK_HEAD()                                 \
T **start_of_call_stack = call_stack;                \
*call_stack++ = start;                               \
*call_stack++ = end;                                 \
do {                                                 \
    right = *--call_stack, left = *--call_stack

// Instead of recursive calls, the required variables are put on the stack.
#define QUICK_CALL(name, l, r) do {*call_stack++ = l; *call_stack++ = r;} while (false)

// Closing the loop which pops from the stack.
#define QUICK_TAIL() } while (call_stack != start_of_call_stack)

// Obviously, ending the current QuickSort is done via `continue`.
#define QUICK_STOP() continue

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
    T *left, *right;
    QUICK_HEAD();
    if (right <= left) QUICK_STOP();
    if (right - left + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(left, right);
        QUICK_STOP();
    }
    QUICK_BODY();
    QUICK_CALL(quick_sort, left, i - 1);
    QUICK_CALL(quick_sort, i + 1, right);
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort where the trivial case is not checked.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_no_triviality(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
    if (right - left + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(left, right);
        QUICK_STOP();
    }
    QUICK_BODY();
    QUICK_CALL(quick_sort_no_triviality, left, i - 1);
    QUICK_CALL(quick_sort_no_triviality, i + 1, right);
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort where the triviality is checked after the threshold.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_triviality_after_threshold(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
    if (right - left + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(left, right);
        QUICK_STOP();
    }
    if (right <= left) return;
    QUICK_BODY();
    QUICK_CALL(quick_sort_triviality_after_threshold, left, i - 1);
    QUICK_CALL(quick_sort_triviality_after_threshold, i + 1, right);
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort
 * where the trivial case (partition length <= 1) is checked before recursive calls.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_check_trivial_before_call(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
    if (right - left + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(left, right);
        QUICK_STOP();
    }
    QUICK_BODY();
    if (i - 1 > left)
        QUICK_CALL(quick_sort_check_trivial_before_call, left, i - 1);
    if (right > i + 1)
        QUICK_CALL(quick_sort_check_trivial_before_call, i + 1, right);
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort
 * which terminates if the threshold is undercut.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_no_insertion_sort(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
    if (right - left + 1 <= QUICK_TO_INSERTION) QUICK_STOP();
    QUICK_BODY();
    QUICK_CALL(quick_sort_no_insertion_sort, left, i - 1);
    QUICK_CALL(quick_sort_no_insertion_sort, i + 1, right);
    QUICK_TAIL();
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
    T *left, *right;
    QUICK_HEAD();
    QUICK_BODY();
    if ((i - 1) - left + 1 <= QUICK_TO_INSERTION)
        insertion_sort_sentinel(left, i - 1);
    else
        QUICK_CALL(quick_sort_check_threshold_before_call, left, i - 1);
    if (right - (i + 1) + 1 <= QUICK_TO_INSERTION)
        insertion_sort_sentinel(i + 1, right);
    else
        QUICK_CALL(quick_sort_check_threshold_before_call, i + 1, right);
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort
 * where the triviality and the threshold are checked before recursive calls.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_check_triviality_and_threshold_before_call(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
    QUICK_BODY();
    if ((i - 1) - left + 1 <= QUICK_TO_INSERTION) {
        if (i - 1 > left)
            insertion_sort_sentinel(left, i - 1);
    } else
        QUICK_CALL(quick_sort_check_triviality_and_threshold_before_call, left, i - 1);
    if (right - (i + 1) + 1 <= QUICK_TO_INSERTION) {
        if (right > i + 1)
            insertion_sort_sentinel(i + 1, right);
    } else
        QUICK_CALL(quick_sort_check_triviality_and_threshold_before_call, i + 1, right);
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort
 * where the triviality is checked within the threshold check.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_triviality_within_threshold(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
    if (right - left + 1 <= QUICK_TO_INSERTION) {
        if (right > left)
            insertion_sort_sentinel(left, right);
        QUICK_STOP();
    }
    QUICK_BODY();
    QUICK_CALL(quick_sort_triviality_within_threshold, left, i - 1);
    QUICK_CALL(quick_sort_triviality_within_threshold, i + 1, right);
    QUICK_TAIL();
}

#if (!RECURSIVE)
/**
 * @brief An implementation of the fastest variant of the iterative QuickSort
 * where the last push to the call stack is replaced by a simple jump.
 * 
 * @param start 
 * @param end 
 */
static void quick_sort_optimised_iterative(T * const start, T * const end) {
    T *left, *right;
    QUICK_HEAD();
optimised_label:
    if (right - left + 1 <= QUICK_TO_INSERTION) {
        insertion_sort_sentinel(left, right);
        QUICK_STOP();
    }
    QUICK_BODY();
    QUICK_CALL(quick_sort_triviality_within_threshold, left, i - 1);
    left = i + 1;
    goto optimised_label;
    QUICK_TAIL();
}
#endif

int main() {
    triple_buffers buffers;
    allocate_triple_buffer(&buffers);
    if (me() != 0) return EXIT_SUCCESS;
    if (DPU_INPUT_ARGUMENTS.mode == 0) {  // called via debugger?
        DPU_INPUT_ARGUMENTS.mode = 2;
        DPU_INPUT_ARGUMENTS.n_reps = 10000;
        DPU_INPUT_ARGUMENTS.upper_bound = 0;
    }

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
#if (!RECURSIVE)
        { quick_sort_optimised_iterative, "Optimised" }
#endif
    };
    size_t lengths[] = { 20, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
    size_t num_of_algos = sizeof algos / sizeof algos[0];
    size_t num_of_lengths = sizeof lengths / sizeof lengths[0];
    assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    /* Reserve memory for custom call stack, which is needed by the iterative QuickSort. */
#if (!RECURSIVE)
    // 20 pointers on the stack was the most I’ve seen for 1024 elements
    // so the space reserved here should be enough.
    size_t const log = 31 - __builtin_clz(lengths[num_of_lengths - 1]);
    call_stack = mem_alloc(4 * log * sizeof(T *));
#else
    (void)call_stack;
#endif

    test_algos(name, algos, num_of_algos, lengths, num_of_lengths, &buffers, &DPU_INPUT_ARGUMENTS);
    return EXIT_SUCCESS;
}
