/**
 * @file
 * @brief Measures runtimes of sorting algorithms used on full WRAM.
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
T __mram_noinit input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot
static T *call_stacks[NR_TASKLETS][40];  // call stack for iterative QuickSort

// The input length at which QuickSort changes to InsertionSort.
#define QUICK_TO_INSERTION (13)
// The input length at which stable QuickSort changes to InsertionSort.
#define STABLE_QUICK_TO_INSERTION (40)
// The input length at which HeapSort changes to InsertionSort.
#define HEAP_TO_INSERTION (12)
// The length of the first runs sorted by InsertionSort/ShellSort.
#define MERGE_TO_INSERTION (8)

/// @attention Never call this by yourself! Only ever call `insertion_sort_sentinel`!
static void insertion_sort_sentinel_helper(T * const start, T * const end) {
    T *curr, *i = start;
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
    insertion_sort_sentinel_helper(start + 1, end);
}

/**
 * @brief An implementation of InsertionSort with variable step size.
 * Needed by ShellSort.
 * @attention This algorithm relies on `start[-step .. -1]` being a sentinel value,
 * i.e. being at least as small as any value in the array.
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
 * @brief A two-round ShellSort with step sizes 3 and 1.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static __attribute__((unused)) void shell_sort(T * const start, T * const end) {
    for (size_t j = 0; j < 4; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 4);
    insertion_sort_sentinel(start, end);
}

/**
 * @brief The fastest implementation of QuickSort, which uses a manual call stack.
 * To this end, enough memory should be reserved
 * and saved in the file-wide variable `start_of_call_stack`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void quick_sort(T * const start, T * const end) {
    T ** const start_of_call_stack = &call_stacks[me()][0];
    T **call_stack = start_of_call_stack;
    *call_stack++ = start;
    *call_stack++ = end;
    do {
        /* Pop from call stack. */
        T *right = *--call_stack;
        T *left = *--call_stack;
        /* Detect base cases. */
        if (right - left + 1 <= QUICK_TO_INSERTION) {
            if (right > left)
                insertion_sort_sentinel(left, right);
            continue;
        }
        /* Put elements into respective partitions. */
        T * const pivot = get_pivot(left, right);  // The pivot acts as sentinel value.
        swap(pivot, right);
        T *i = left - 1, *j = right;
        while (true) {
            while (*++i < *right);
            while (*--j > *right);
            if (i >= j) break;
            swap(i, j);
        }
        swap(i, right);
        /* Push left partition to call stack. */
        *call_stack++ = left;
        *call_stack++ = i - 1;
        /* Push right partition to call stack. */
        *call_stack++ = i + 1;
        *call_stack++ = right;
    } while (call_stack != start_of_call_stack);
}

// @todo Work in progress
static void quick_sort_stable_with_arrays(T * const start, T * const end) {
    T * const smaller = end + 1, * const greater = end + 257;
    T ** const start_of_call_stack = &call_stacks[me()][0];
    T **call_stack = start_of_call_stack;
    *call_stack++ = start;
    *call_stack++ = end;
    do {
        /* Pop from call stack. */
        T *right = *--call_stack;
        T *left = *--call_stack;
        /* Detect base cases. */
        if (right - left + 1 <= STABLE_QUICK_TO_INSERTION) {
            if (right > left)
                insertion_sort_sentinel(left, right);
            continue;
        }
        /* Put elements into respective partitions. */
        T * const pivot = get_pivot(left, right);
        T *s = smaller - 1, *g = greater - 1; 
        for (T *i = left; i <= right; i++) {
            if (*i < *pivot) {
                *++s = *i;
            } else if (*i > *pivot) {
                *++g = *i;
            } else if (i < pivot) {
                *++s = *i;
            } else if (i > pivot) {
                *++g = *i;
            }
        }
        /* Move elements back into the array. */
        T const pivot_value = *pivot;
        T *orig_array = left;
        for (T *i = smaller; i <= s; i++) {
            *orig_array++ = *i;
        }
        *orig_array++ = pivot_value;
        for (T *i = greater; i <= g; i++) {
            *orig_array++ = *i;
        }
        /* Push left partition to call stack. */
        *call_stack++ = left;
        *call_stack++ = left + (s - smaller);
        /* Push right partition to call stack. */
        *call_stack++ = left + (s - smaller) + 2;
        *call_stack++ = right;
    } while (call_stack != start_of_call_stack);
}

// @todo Work in progress
static void quick_sort_stable_with_ids(T * const start, T * const end) {
    /* Create array of indices used for distinguishing equivalent elements. */
    for (T *t = end + 1; t <= end + (end - start + 1); t++) {
        *t = (T)t;
    }
    uintptr_t offset = end - start + 1;
    /* Start of actual QuickSort. */
    T ** const start_of_call_stack = &call_stacks[me()][0];
    T **call_stack = start_of_call_stack;
    *call_stack++ = start;
    *call_stack++ = end;
    do {
        /* Pop from call stack. */
        T *right = *--call_stack;
        T *left = *--call_stack;
        /* Detect base cases. */
        if (right - left + 1 <= QUICK_TO_INSERTION) {
            if (right > left)
                insertion_sort_sentinel(left, right);
            continue;
        }
        /* Put elements into respective partitions. */
        T * const pivot = get_pivot(left, right);  // The pivot acts as sentinel value.
        swap(pivot, right);
        swap(pivot + offset, right + offset);
        T *i = left - 1, *j = right;
        while (true) {
            while (*++i < *right);
            while (*--j > *right);
            if (i >= j) break;
            swap(i, j);
            swap(i + offset, j + offset);
        }
        swap(i, right);
        swap(i + offset, right + offset);
        /* Push left partition to call stack. */
        *call_stack++ = left;
        *call_stack++ = i - 1;
        /* Push right partition to call stack. */
        *call_stack++ = i + 1;
        *call_stack++ = right;
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
        if (heap[son + 1] > heap[son]) {  // Check if right son is bigger.
            if (heap[son + 1] <= root_value) break;
            heap[father] = heap[son + 1];  // Shift right son up.
            father = son + 1;
        } else {
            if (heap[son] <= root_value) break;
            heap[father] = heap[son];  // Shift left son up.
            father = son;
        }
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
    start[n] = T_MIN;
    for (size_t r = n / 2; r > 0; r--) {
        heapify(start, n, r - 1);
    }
    /* Sort by repeatedly putting the root at the end of the heap. */
    if (!(n & 1)) {  // If `n' is even, the last leaf is a left one. (cf. loop below)
        swap(&start[0], &start[--n]);
        heapify(start, n, 0);
    }
    // `i` is always odd. When there is an odd number of elements, the last leaf is a right one.
    // Pulling it to the front raises the need for a sentinel value,
    // since the leaf with which one ends up at the end of `heapify` may be the last one,
    // which is now a left one. Since the right brothers of nodes are checked,
    // a sentinel value erases the need for an additional bounds check.
    size_t i;
    for (i = n - 1; i > HEAP_TO_INSERTION; i -= 2) {
        T const biggest_element = start[0];
        start[0] = start[i];
        start[i] = T_MIN;
        heapify(start, i, 0);
        start[i] = biggest_element;

        swap(&start[0], &start[i - 1]);
        heapify(start, i - 1, 0);
    }
    insertion_sort_sentinel(start, &start[i]);
}

// Creating the starting runs for MergeSort …
#if (MERGE_TO_INSERTION <= 20)
// … using InsertionSort.
#define CREATE_STARTING_RUNS()                                                                 \
if (end - start + 1 <= MERGE_TO_INSERTION) {                                                   \
    insertion_sort_sentinel(start, end);                                                       \
    flag = true;                                                                               \
    return;                                                                                    \
}                                                                                              \
insertion_sort_sentinel(start, start + MERGE_TO_INSERTION - 1);                                \
for (T *t = start + MERGE_TO_INSERTION; t < end; t += MERGE_TO_INSERTION) {                    \
    T const before_sentinel = *(t - 1);                                                        \
    *(t - 1) = T_MIN;  /* Set sentinel value. */                                               \
    T * const run_end = (t + MERGE_TO_INSERTION - 1 > end) ? end : t + MERGE_TO_INSERTION - 1; \
    insertion_sort_sentinel(t, run_end);                                                       \
    *(t - 1) = before_sentinel;  /* Restore old value. */                                      \
}
#else
// … using ShellSort.
#define CREATE_STARTING_RUNS()                                                                 \
if (end - start + 1 <= MERGE_TO_INSERTION) {                                                   \
    shell_sort(start, end);                                                                    \
    flag = true;                                                                               \
    return;                                                                                    \
}                                                                                              \
shell_sort(start, start + MERGE_TO_INSERTION - 1);                                             \
for (T *t = start + MERGE_TO_INSERTION; t < end; t += MERGE_TO_INSERTION) {                    \
    T const before_sentinel[4] = { *(t - 1), *(t - 2), *(t - 3), *(t - 4) };                   \
    *(t - 1) = T_MIN;  /* Set sentinel values. */                                              \
    *(t - 2) = T_MIN;                                                                          \
    *(t - 3) = T_MIN;                                                                          \
    *(t - 4) = T_MIN;                                                                          \
    T * const run_end = (t + MERGE_TO_INSERTION - 1 > end) ? end : t + MERGE_TO_INSERTION - 1; \
    shell_sort(t, run_end);                                                                    \
    *(t - 1) = before_sentinel[0];  /* Restore old values. */                                  \
    *(t - 2) = before_sentinel[1];                                                             \
    *(t - 3) = before_sentinel[2];                                                             \
    *(t - 4) = before_sentinel[3];                                                             \
}
#endif

/**
 * @brief Merges two runs ranging from [`start_1`, `start_2`[ and [`start_2`, `end_2`].
 * @internal Using `end_1 = start_2 - 1` worsens the runtime
 * but making `end_2` exclusive also worsens the runtime, hence the asymmetry.
 * 
 * @param start_1 The first element of the first run.
 * @param start_2 The first element of the second run. Must be follow the end of the first run.
 * @param end_2 The last element of the second run.
 * @param out Whither the merged runs are written.
**/
static inline void merge(T * const start_1, T * const start_2, T * const end_2, T *out) {
    T *i = start_1, *j = start_2;
    while (true) {
        if (*i <= *j) {
            *out++ = *i++;
            if (i >= start_2) {  // Pulling these if-statements out of the loop …
                do {
                    *out++ = *j++;
                } while (j <= end_2);
                return;
            }
        } else {
            *out++ = *j++;
            if (j > end_2) {  // … worsens the runtime.
                do {
                    *out++ = *i++;
                } while (i < start_2);
                return;
            }
        }
    }
}

// If false, the final sorted array is still in an auxiliary array, such that a write-back is needed.
bool flag = true;

/**
 * @brief An implementation of standard MergeSort.
 * @note This function saves up to `n` elements after the end of the input array.
 * For speed reasons, the sorted array may be stored after that very end.
 * In other words, the sorted array is not written back to the start of the input array.
 * @internal `inline` does seem to help‽ I only measured the runtimes, though.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static inline void merge_sort_no_write_back(T * const start, T * const end) {
    /* Natural runs. */
    CREATE_STARTING_RUNS();
    /* Merging. */
    T *in, *until, *out;  // Runs from `in` to `until` are merged and stored in `out`.
    flag = true;  // Used to determine the initial positions of `in`, `out`, and `until`.
    size_t const n = end - start + 1;
    for (size_t run_length = MERGE_TO_INSERTION; run_length < n; run_length *= 2) {
        // Set the positions to read from and write to.
        if ((flag = !flag)) {
            in = end + 1;
            until = end + n;
            out = start;
        } else {
            in = start;
            until = end;
            out = end + 1;
        }
        // Merge pairs of adjacent runs.
        for (; in <= until; in += 2 * run_length, out += 2 * run_length) {
            // Only one run left?
            if (in + run_length - 1 >= until) {
                do {
                    *out++ = *in++;
                } while (in <= until);
                break;
            }
            // If not, merge the next two runs.
            T * const run_2_end = (in + 2 * run_length - 1 > until) ? until : in + 2 * run_length - 1;
            merge(in, in + run_length, run_2_end, out);
        }
    }
}

/**
 * @brief An implementation of standard MergeSort.
 * @note This function saves up to `n` elements after the end of the input array.
 * The sorted array is always stored from `start` to `end`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void merge_sort_write_back(T * const start, T * const end) {
    merge_sort_no_write_back(start, end);
    /* Writing back. */
    if (flag)
        return;
    T *in = end + 1, *until = end + (end - start) + 1, *out = start;
    do {
        *out++ = *in++;
    } while (in <= until);
}

/**
 * @brief Merges two runs ranging from [`start_1`, `end_1`] and [`start_2`, `end_2`].
 * If the first run is depleted, the second one will not be flushed.
 * 
 * @param start_1 The first element of the first run.
 * @param end_1 The last element of the first run.
 * @param start_2 The first element of the second run.
 * @param end_2 The last element of the second run.
 * @param out Whither the merged runs are written.
 * Must be equal to `start_2` - (`end_1` - `start_1` + 1).
**/
static inline void merge_left_flush_only(T * const start_1, T * const end_1, T * const start_2,
        T * const end_2, T *out) {
    T *i = start_1, *j = start_2;
    while (true) {
        if (*i <= *j) {
            *out++ = *i++;
            if (i > end_1) {
                return;
            }
        } else {
            *out++ = *j++;
            if (j > end_2) {
                do {
                    *out++ = *i++;
                } while (i <= end_1);
                return;
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
static void merge_sort_half_space(T * const start, T * const end) {
    /* Natural runs. */
    CREATE_STARTING_RUNS();
    /* Merging. */
    size_t const n = end - start + 1;
    for (size_t run_length = MERGE_TO_INSERTION; run_length < n; run_length *= 2) {
        // Merge pairs of adjacent runs.
        for (T *i = start; i <= end; i += 2 * run_length) {
            // Only one run left?
            if (i + run_length - 1 >= end) {
                break;
            }
            // If not, copy the current run …
            T *out = end + 1, *j = i;
            do {
                *out++ = *j++;
            } while (j < i + run_length);
            // … and merge the copy with the next run.
            T * const run_2_end = (i + 2 * run_length - 1 > end) ? end : i + 2 * run_length - 1;
            merge_left_flush_only(end + 1, end + run_length, i + run_length, run_2_end, i);
        }
    }
}

union algo_to_test __host algos[] = {
    {{ "Quick", quick_sort }},
    // {{ "QuickStable", quick_sort_stable_with_arrays }},
    // {{ "QuickStableIds", quick_sort_stable_with_ids }},
    {{ "Heap", heap_sort }},
    {{ "Merge", merge_sort_no_write_back}},
    {{ "MergeWriteBack", merge_sort_write_back }},
    {{ "MergeHalfSpace", merge_sort_half_space }},
};
size_t __host lengths[] = { 20, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768 };//, 1024 };
size_t __host num_of_algos = sizeof algos / sizeof algos[0];
size_t __host num_of_lengths = sizeof lengths / sizeof lengths[0];

int main() {
    if (me() != 0) return EXIT_SUCCESS;

    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
        /* Add additional sentinel values. */
        size_t num_of_sentinels = 8;  // 4 is the maximum step, 8 ensures alignment.
        for (size_t i = 0; i < num_of_sentinels; i++)
            buffers[me()].cache[i] = T_MIN;
        buffers[me()].cache += num_of_sentinels;
        assert(lengths[num_of_lengths - 1] + num_of_sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
        assert(!((uintptr_t)buffers[me()].cache & 7) && "Cache address not aligned on 8 bytes!");
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.length = 256;
        host_to_dpu.reps = 1;
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }

    /* Perform test. */
    T __mram_ptr *read_from = input;
    T * const start = cache, * const end = &cache[host_to_dpu.length - 1];
    unsigned int const transfer_size = ROUND_UP_POW2(sizeof(T[host_to_dpu.length]), 8);
    base_sort_algo * const algo = algos[host_to_dpu.algo_index].data.fct;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());
        mram_read(read_from, cache, transfer_size);

        array_stats stats_before;
        get_stats_unsorted_wram(cache, host_to_dpu.length, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        time new_time = perfcounter_get();
        algo(start, end);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
        dpu_to_host.firsts += new_time;
        dpu_to_host.seconds += new_time * new_time;

        size_t offset = 0;  // Needed because of the MergeSort not writing back.
        if (!flag) {
            offset = host_to_dpu.length;
            cache[host_to_dpu.length - 1] = T_MIN;  // `get_stats_sorted_wram` relies on sentinels.
            flag = true;  // Following sorting algorithms may not reset this value.
        }
        array_stats stats_after;
        get_stats_sorted_wram(cache + offset, host_to_dpu.length, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        read_from += host_to_dpu.length;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
