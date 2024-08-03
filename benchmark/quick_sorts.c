/**
 * @file
 * @brief Measures runtimes of some implementations of QuickSort.
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <alloc.h>
#include <defs.h>
#include <perfcounter.h>

#include "buffers.h"
#include "checkers.h"
#include "common.h"
#include "communication.h"
#include "pivot.h"
#include "random_distribution.h"
#include "random_generator.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot
static T *call_stacks[NR_TASKLETS][40];  // call stack for iterative QuickSort

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

// Wether the left-hand or right-hand partition is done first has an impact on the runtime.
#define SHORTER 1
#define LEFT 2
#define RIGHT 3
#if (PARTITION_PRIO == SHORTER)

#if (RECURSIVE)
// Returns the smaller of the two partitions.
#define QUICK_GET_SHORTER_PARTITION() (i - left <= right - i)
#else  // RECURSIVE
// Returns the smaller of the two partitions.
#define QUICK_GET_SHORTER_PARTITION() (i - left >= right - i)
#endif  // RECURSIVE

#elif (PARTITION_PRIO == LEFT)

// Returns the left-hand partition.
#define QUICK_GET_SHORTER_PARTITION() (RECURSIVE)

#elif (PARTITION_PRIO == RIGHT)

// Returns the right-hand partition.
#define QUICK_GET_SHORTER_PARTITION() (!RECURSIVE)

#endif  // PARTITION_PRIO == RIGHT

// Whether the partition has a length below the threshold.
#define QUICK_IS_THRESHOLD_UNDERCUT() (right - left + 1 <= QUICK_THRESHOLD)

// Whether the partition has a length in { 1, 0, -1 }.
#define QUICK_IS_TRIVIAL() (right <= left)

#define QUICK_FALLBACK() insertion_sort_sentinel(left, right)

#define QUICK_IS_TRIVIAL_LEFT() (i - 1 <= left)
#define QUICK_IS_TRIVIAL_RIGHT() (right <= i + 1)

#define QUICK_IS_THRESHOLD_UNDERCUT_LEFT() ((i - 1) - left + 1 <= QUICK_THRESHOLD)
#define QUICK_IS_THRESHOLD_UNDERCUT_RIGHT() (right - (i + 1) + 1 <= QUICK_THRESHOLD)

#define QUICK_FALLBACK_LEFT() insertion_sort_sentinel(left, i - 1)
#define QUICK_FALLBACK_RIGHT() insertion_sort_sentinel(i + 1, right)

#if (RECURSIVE)  // recursive variant

// Sets up `left` and `right` as synonyms for `start` and `end`.
// They are distinct only in the iterative variant.
#define QUICK_HEAD() T *left = start, *right = end

// Performs a recursive call.
#define QUICK_CALL(name, l, r) name(l, r)

// Unneeded for the recursive variant.
#define QUICK_TAIL()

// Obviously, ending the current QuickSort is done via `return`.
#define QUICK_STOP() return

#define QUICK_CALL_LEFT(name) name(left, i - 1)
#define QUICK_CALL_RIGHT(name) name(i + 1, right)

#else  // RECURSIVE

// A “call” stack for holding the values of `left` and `right` is maintained.
// Its memory must be reserved beforehand.
#define QUICK_HEAD()                                    \
T ** const start_of_call_stack = &call_stacks[me()][0]; \
T **call_stack = start_of_call_stack;                   \
*call_stack++ = start;                                  \
*call_stack++ = end;                                    \
do {                                                    \
    T *right = *--call_stack, *left = *--call_stack

// Instead of recursive calls, the required variables are put on the stack.
#define QUICK_CALL(name, l, r) do {*call_stack++ = l; *call_stack++ = r;} while (false)

// Closing the loop which pops from the stack.
#define QUICK_TAIL() } while (call_stack != start_of_call_stack)

// Obviously, ending the current QuickSort is done via `continue`.
#define QUICK_STOP() continue

#define QUICK_CALL_LEFT(name) do { *call_stack++ = left; *call_stack++ = i - 1; } while (false)
#define QUICK_CALL_RIGHT(name) do { *call_stack++ = i + 1; *call_stack++ = right; } while (false)

#endif  // RECURSIVE

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
 * @brief The fastest implementation of QuickSort.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort(T * const start, T * const end) {
    QUICK_HEAD();
    if (QUICK_IS_TRIVIAL()) QUICK_STOP();
    if (QUICK_IS_THRESHOLD_UNDERCUT()) {
        QUICK_FALLBACK();
        QUICK_STOP();
    }
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        QUICK_CALL_LEFT(quick_sort);
        QUICK_CALL_RIGHT(quick_sort);
    } else {
        QUICK_CALL_RIGHT(quick_sort);
        QUICK_CALL_LEFT(quick_sort);
    }
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort where the trivial case is not checked.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_no_triviality(T * const start, T * const end) {
    QUICK_HEAD();
    if (QUICK_IS_THRESHOLD_UNDERCUT()) {
        QUICK_FALLBACK();
        QUICK_STOP();
    }
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        QUICK_CALL_LEFT(quick_sort_no_triviality);
        QUICK_CALL_RIGHT(quick_sort_no_triviality);
    } else {
        QUICK_CALL_RIGHT(quick_sort_no_triviality);
        QUICK_CALL_LEFT(quick_sort_no_triviality);
    }
    QUICK_TAIL();
}

/**
 * @brief An implementation of QuickSort where the triviality is checked after the threshold.
 *
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort_triviality_after_threshold(T * const start, T * const end) {
    QUICK_HEAD();
    if (QUICK_IS_THRESHOLD_UNDERCUT()) {
        QUICK_FALLBACK();
        QUICK_STOP();
    }
    if (QUICK_IS_TRIVIAL()) QUICK_STOP();
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        QUICK_CALL_LEFT(quick_sort_triviality_after_threshold);
        QUICK_CALL_RIGHT(quick_sort_triviality_after_threshold);
    } else {
        QUICK_CALL_RIGHT(quick_sort_triviality_after_threshold);
        QUICK_CALL_LEFT(quick_sort_triviality_after_threshold);
    }
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
    QUICK_HEAD();
    if (QUICK_IS_THRESHOLD_UNDERCUT()) {
        QUICK_FALLBACK();
        QUICK_STOP();
    }
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        if (!QUICK_IS_TRIVIAL_LEFT())
            QUICK_CALL_LEFT(quick_sort_check_trivial_before_call);
        if (!QUICK_IS_TRIVIAL_RIGHT())
            QUICK_CALL_RIGHT(quick_sort_check_trivial_before_call);
    } else {
        if (!QUICK_IS_TRIVIAL_RIGHT())
            QUICK_CALL_RIGHT(quick_sort_check_trivial_before_call);
        if (!QUICK_IS_TRIVIAL_LEFT())
            QUICK_CALL_LEFT(quick_sort_check_trivial_before_call);
    }
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
    QUICK_HEAD();
    if (QUICK_IS_THRESHOLD_UNDERCUT())
        QUICK_STOP();
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        QUICK_CALL_LEFT(quick_sort_no_insertion_sort);
        QUICK_CALL_RIGHT(quick_sort_no_insertion_sort);
    } else {
        QUICK_CALL_RIGHT(quick_sort_no_insertion_sort);
        QUICK_CALL_LEFT(quick_sort_no_insertion_sort);
    }
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
    QUICK_HEAD();
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        if (QUICK_IS_THRESHOLD_UNDERCUT_LEFT())
            QUICK_FALLBACK_LEFT();
        else
            QUICK_CALL_LEFT(quick_sort_check_threshold_before_call);
        if (QUICK_IS_THRESHOLD_UNDERCUT_RIGHT())
            QUICK_FALLBACK_RIGHT();
        else
            QUICK_CALL_RIGHT(quick_sort_check_threshold_before_call);
    } else {
        if (QUICK_IS_THRESHOLD_UNDERCUT_RIGHT())
            QUICK_FALLBACK_RIGHT();
        else
            QUICK_CALL_RIGHT(quick_sort_check_threshold_before_call);
        if (QUICK_IS_THRESHOLD_UNDERCUT_LEFT())
            QUICK_FALLBACK_LEFT();
        else
            QUICK_CALL_LEFT(quick_sort_check_threshold_before_call);
    }
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
    QUICK_HEAD();
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        if (QUICK_IS_THRESHOLD_UNDERCUT_LEFT()) {
            if (!QUICK_IS_TRIVIAL_LEFT())
                QUICK_FALLBACK_LEFT();
        } else
            QUICK_CALL_LEFT(quick_sort_check_triviality_and_threshold_before_call);
        if (QUICK_IS_THRESHOLD_UNDERCUT_RIGHT()) {
            if (!QUICK_IS_TRIVIAL_RIGHT())
                QUICK_FALLBACK_RIGHT();
        } else
            QUICK_CALL_RIGHT(quick_sort_check_triviality_and_threshold_before_call);
    } else {
        if (QUICK_IS_THRESHOLD_UNDERCUT_RIGHT()) {
            if (!QUICK_IS_TRIVIAL_RIGHT())
                QUICK_FALLBACK_RIGHT();
        } else
            QUICK_CALL_RIGHT(quick_sort_check_triviality_and_threshold_before_call);
        if (QUICK_IS_THRESHOLD_UNDERCUT_LEFT()) {
            if (!QUICK_IS_TRIVIAL_LEFT())
                QUICK_FALLBACK_LEFT();
        } else
            QUICK_CALL_LEFT(quick_sort_check_triviality_and_threshold_before_call);
    }
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
    QUICK_HEAD();
    if (QUICK_IS_THRESHOLD_UNDERCUT()) {
        if (!QUICK_IS_TRIVIAL())
            QUICK_FALLBACK();
        QUICK_STOP();
    }
    QUICK_BODY();
    if (QUICK_GET_SHORTER_PARTITION()) {
        QUICK_CALL_LEFT(quick_sort_triviality_within_threshold);
        QUICK_CALL_RIGHT(quick_sort_triviality_within_threshold);
    } else {
        QUICK_CALL_RIGHT(quick_sort_triviality_within_threshold);
        QUICK_CALL_LEFT(quick_sort_triviality_within_threshold);
    }
    QUICK_TAIL();
}

union algo_to_test __host algos[] = {
    {{ "Normal", quick_sort }},
    {{ "TrivialBC", quick_sort_check_trivial_before_call }},
    {{ "NoTrivial", quick_sort_no_triviality }},
    {{ "OneInsertion", sort_with_one_insertion_sort }},
    {{ "ThreshBC", quick_sort_check_threshold_before_call }},
    {{ "ThreshTrivBC", quick_sort_check_triviality_and_threshold_before_call }},
    {{ "ThreshThenTriv", quick_sort_triviality_after_threshold }},
    {{ "TrivInThresh", quick_sort_triviality_within_threshold }},
};
size_t __host lengths[] = { 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
size_t __host num_of_algos = sizeof algos / sizeof algos[0];
size_t __host num_of_lengths = sizeof lengths / sizeof lengths[0];

int main(void) {
    if (me() != 0) return EXIT_SUCCESS;
    (void)call_stacks;

    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
        assert(lengths[num_of_lengths - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.reps = 100;
        host_to_dpu.length = lengths[0];
        host_to_dpu.offset = ROUND_UP_POW2(host_to_dpu.length * sizeof(T), 8) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 0);
    }
    /* Perform test. */
    T __mram_ptr *read_from = input;
    T * const start = cache, * const end = &cache[host_to_dpu.length - 1];
    unsigned int const transfer_size = ROUND_UP_POW2(sizeof(T[host_to_dpu.length]), 8);
    base_sort_algo * const algo = algos[host_to_dpu.algo_index].data.fct;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());
        mram_read_triple(read_from, cache, transfer_size);

        array_stats stats_before;
        get_stats_unsorted_wram(cache, host_to_dpu.length, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        time new_time = perfcounter_get();
        algo(start, end);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
        dpu_to_host.firsts += new_time;
        dpu_to_host.seconds += new_time * new_time;

        array_stats stats_after;
        get_stats_sorted_wram(cache, host_to_dpu.length, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        read_from += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
