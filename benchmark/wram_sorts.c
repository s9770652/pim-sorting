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
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot
static T *call_stacks[NR_TASKLETS][40];  // call stack for iterative QuickSort
static bool flags[NR_TASKLETS];  // Whether a write-back from the auxiliary array is (not) needed.

// The input length at which QuickSort changes to InsertionSort.
#define QUICK_TO_INSERTION (13)
// The input length at which stable QuickSort changes to InsertionSort.
#define STABLE_QUICK_TO_INSERTION (40)
// The input length at which HeapSort changes to InsertionSort.
#define HEAP_TO_INSERTION (12)
// The number of elements flushed at once if possible.
#define FLUSH_BATCH_LENGTH (24)

#if (MERGE_TO_SHELL > 48)
#define FIRST_STEP (12)
#elif (MERGE_TO_SHELL > 16)
#define FIRST_STEP (6)
#else
#define FIRST_STEP (1)
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
        T const to_sort = *curr;
        while (*(curr - step) > to_sort) {  // `-step` always valid due to the sentinel value
            *curr = *(curr - step);
            curr -= step;
        }
        *curr = to_sort;
    }
}

/**
 * @brief A ShellSort with one, two, or three passes of InsertionSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static __attribute__((unused)) void shell_sort(T * const start, T * const end) {
#if (MERGE_TO_SHELL > 48)
    for (size_t j = 0; j < 12; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 12);
    for (size_t j = 0; j < 5; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 5);
#elif (MERGE_TO_SHELL > 16)
    for (size_t j = 0; j < 6; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 6);
#endif  // MERGE_TO_SHELL > 16
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

// Creating the starting runs for MergeSort.
#define CREATE_STARTING_RUNS()                                                         \
if (end - start + 1 <= MERGE_TO_SHELL) {                                               \
    shell_sort(start, end);                                                            \
    flags[me()] = false;                                                               \
    return;                                                                            \
}                                                                                      \
shell_sort(start, start + MERGE_TO_SHELL - 1);                                         \
for (T *t = start + MERGE_TO_SHELL; t < end; t += MERGE_TO_SHELL) {                    \
    T before_sentinel[FIRST_STEP];                                                     \
    _Pragma("unroll")  /* Set sentinel values. */                                      \
    for (size_t i = 0; i < FIRST_STEP; i++) {                                          \
        before_sentinel[i] = *(t - i - 1);                                             \
        *(t - i - 1) = T_MIN;                                                          \
    }                                                                                  \
    T * const run_end = (t + MERGE_TO_SHELL - 1 > end) ? end : t + MERGE_TO_SHELL - 1; \
    shell_sort(t, run_end);                                                            \
    _Pragma("unroll")  /* Restore old values. */                                       \
    for (size_t i = 0; i < FIRST_STEP; i++) {                                          \
        *(t - i - 1) = before_sentinel[i];                                             \
    }                                                                                  \
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using batches of length `FLUSH_BATCH_LENGTH`:
 * If there are at least `FLUSH_BATCH_LENGTH` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `FLUSH_BATCH_LENGTH`th in good cases.
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void flush_batch(T *in, T *until, T *out) {
    while (in + FLUSH_BATCH_LENGTH - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < FLUSH_BATCH_LENGTH; k++)
            *(out + k) = *(in + k);
        out += FLUSH_BATCH_LENGTH;
        in += FLUSH_BATCH_LENGTH;
    }
    while (in <= until) {
        *out++ = *in++;
    }
}

/**
 * @brief Copies a given range of values to some sepcified buffer.
 * The copying is done using batches of length `MERGE_TO_SHELL`:
 * If there are at least `MERGE_TO_SHELL` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `MERGE_TO_SHELL`th in good cases.
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void flush_starting_run(T *in, T *until, T *out) {
    while (in + MERGE_TO_SHELL - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < MERGE_TO_SHELL; k++)
            *(out + k) = *(in + k);
        out += MERGE_TO_SHELL;
        in += MERGE_TO_SHELL;
    }
    while (in <= until) {
        *out++ = *in++;
    }
}

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
            if (i == start_2) {  // Pulling these if-statements out of the loop …
                flush_batch(j, end_2, out);
                return;
            }
        } else {
            *out++ = *j++;
            if (j > end_2) {  // … worsens the runtime.
                flush_batch(i, start_2 - 1, out);
                return;
            }
        }
    }
}

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
    bool flag = false;  // Used to determine the initial positions of `in`, `out`, and `until`.
    size_t const n = end - start + 1;
    for (size_t run_length = MERGE_TO_SHELL; run_length < n; run_length *= 2) {
        // Set the positions to read from and write to.
        if ((flag = !flag)) {
            in = start;
            until = end;
            out = end + 1;
        } else {
            in = end + 1;
            until = end + n;
            out = start;
        }
        // Merge pairs of adjacent runs.
        for (; in <= until; in += 2 * run_length, out += 2 * run_length) {
            // Only one run left?
            if (in + run_length > until) {
                flush_starting_run(in, until, out);
                break;
            }
            // If not, merge the next two runs.
            T * const run_2_end = (in + 2 * run_length - 1 > until) ? until : in + 2 * run_length - 1;
            merge(in, in + run_length, run_2_end, out);
        }
    }
    flags[me()] = flag;
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
    if (!flags[me()])
        return;
    T *in = end + 1, *until = end + (end - start) + 1, *out = start;
    flush_starting_run(in, until, out);
}

/**
 * @brief Merges two runs ranging from [`start_1`, `end_1`] and [`start_2`, `end_2`].
 * If the second run is depleted, the first one will not be flushed.
 * 
 * @param start_1 The first element of the first run.
 * @param end_1 The last element of the first run.
 * @param start_2 The first element of the second run.
 * @param end_2 The last element of the second run.
 * @param out Whither the merged runs are written.
 * Must be equal to `start_1` - (`end_2` - `start_2` + 1).
**/
static inline void merge_right_flush_only(T * const start_1, T * const end_1, T * const start_2,
        T * const end_2, T *out) {
    T *i = start_1, *j = start_2;
    while (true) {
        if (*i < *j) {
            *out++ = *i++;
            if (i > end_1) {
                flush_batch(j, end_2, out);
                return;
            }
        } else {
            *out++ = *j++;
            if (j > end_2) {
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
    for (size_t run_length = MERGE_TO_SHELL; run_length < n; run_length *= 2) {
        // Merge pairs of adjacent runs.
        for (T *i = start; i <= end; i += 2 * run_length) {
            // Only one run left?
            if (i + run_length - 1 >= end) {
                break;
            }
            // If not, copy the current run …
            T *out = end + 1, *j = i;
            do {
                #pragma unroll
                for (size_t k = 0; k < MERGE_TO_SHELL; k++)
                    *(out + k) = *(j + k);
                out += MERGE_TO_SHELL;
                j += MERGE_TO_SHELL;
            } while (j < i + run_length);
            // … and merge the copy with the next run.
            T * const run_1_end = (i + 2 * run_length - 1 > end) ? end : i + 2 * run_length - 1;
            merge_right_flush_only(i + run_length, run_1_end, end + 1, end + run_length, i);
        }
    }
}

// /// @brief The copying of the first run happens only if merging is needed.
// static inline void merge_left_flush_only(T * const start_1, T * const start_2,
//         T * const end_2, T * aux, size_t const run_length) {
//     T *i = start_1, *j = start_2;
//     while (true) {
//         if (*i <= *j) {
//             i++;
//             if (i == start_2)
//                 return;  // Everything in the first run was smaller than in the second one.
//         } else {
//             flush_starting_run(i, start_2 - 1, aux);
//             *i = *j++;
//             break;
//         }
//     }
//     T *aux_end = aux + run_length - (i - start_1) - 1, *out = i + 1;
//     i = aux;
//     while (true) {
//         if (*i <= *j) {
//             *out++ = *i++;
//             if (i > aux_end) {
//                 return;
//             }
//         } else {
//             *out++ = *j++;
//             if (j > end_2) {
//                 flush_batch(i, aux_end, out);
//                 return;
//             }
//         }
//     }
// }

// static void merge_sort_half_space(T * const start, T * const end) {
//     /* Natural runs. */
//     CREATE_STARTING_RUNS();
//     /* Merging. */
//     size_t const n = end - start + 1;
//     for (size_t run_length = MERGE_TO_SHELL; run_length < n; run_length *= 2) {
//         // Merge pairs of adjacent runs.
//         for (T *i = start; i <= end; i += 2 * run_length) {
//             // Only one run left?
//             if (i + run_length - 1 >= end) {
//                 break;
//             }
//             // … and merge the copy with the next run.
//             T * const end_2 = (i + 2 * run_length - 1 > end) ? end : i + 2 * run_length - 1;
//             merge_left_flush_only(i, i + run_length, end_2, end + 1, run_length);
//             // merge_left_flush_only(i + run_length, end_2, i, end + 1, run_length);
//         }
//     }
// }

union algo_to_test __host algos[] = {
    // {{ "Quick", quick_sort }},
    // {{ "QuickStable", quick_sort_stable_with_arrays }},
    // {{ "QuickStableIds", quick_sort_stable_with_ids }},
    // {{ "Heap", heap_sort }},
    {{ "Merge", merge_sort_no_write_back}},
    {{ "MergeWriteBack", merge_sort_write_back }},
    {{ "MergeHalfSpace", merge_sort_half_space }},
};
size_t __host lengths[] = { 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072 };
// size_t __host lengths[] = { 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
size_t __host num_of_algos = sizeof algos / sizeof algos[0];
size_t __host num_of_lengths = sizeof lengths / sizeof lengths[0];

int main() {
    if (me() != 0) return EXIT_SUCCESS;

    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
        /* Add additional sentinel values. */
        size_t const num_of_sentinels = ROUND_UP_POW2(FIRST_STEP * sizeof(T), 8) / sizeof(T);
        for (size_t i = 0; i < num_of_sentinels; i++)
            buffers[me()].cache[i] = T_MIN;
        buffers[me()].cache += num_of_sentinels;
        assert(2*lengths[num_of_lengths - 1] + num_of_sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
        assert(!((uintptr_t)buffers[me()].cache & 7) && "Cache address not aligned on 8 bytes!");
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = lengths[0];
        host_to_dpu.offset = ROUND_UP_POW2(host_to_dpu.length * sizeof(T), 8) / sizeof(T);
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
        mram_read_triple(read_from, cache, transfer_size);

        array_stats stats_before;
        get_stats_unsorted_wram(cache, host_to_dpu.length, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        time new_time = perfcounter_get();
        algo(start, end);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
        dpu_to_host.firsts += new_time;
        dpu_to_host.seconds += new_time * new_time;

        size_t offset = 0;  // Needed because of the MergeSort not writing back.
        if (flags[me()]) {
            offset = host_to_dpu.length;
            cache[host_to_dpu.length - 1] = T_MIN;  // `get_stats_sorted_wram` relies on sentinels.
            flags[me()] = false;  // Following sorting algorithms may not reset this value.
        }
        array_stats stats_after;
        get_stats_sorted_wram(cache + offset, host_to_dpu.length, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            abort();
        }

        read_from += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
