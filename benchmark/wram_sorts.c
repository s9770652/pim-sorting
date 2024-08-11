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

// The input length at which stable QuickSort changes to InsertionSort.
#define STABLE_QUICK_THRESHOLD (40)
// The input length at which HeapSort changes to InsertionSort.
#define HEAP_TO_INSERTION (15)
static_assert(HEAP_TO_INSERTION & 1, "Applying to right sons, HEAP_TO_INSERTION should be odd!");
// The number of elements flushed at once if possible.
#define FLUSH_BATCH_LENGTH (24)

#if (MERGE_THRESHOLD > 48)
#define FIRST_STEP (12)
#elif (MERGE_THRESHOLD > 16)
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
#if (MERGE_THRESHOLD > 48)
    for (size_t j = 0; j < 12; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 12);
    for (size_t j = 0; j < 5; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 5);
#elif (MERGE_THRESHOLD > 18)
    for (size_t j = 0; j < 6; j++)
        insertion_sort_with_steps_sentinel(&start[j], end, 6);
#endif  // MERGE_THRESHOLD > 18
    insertion_sort_sentinel(start, end);
}

/**
 * @brief Sifts the root of a binary max-heap down.
 * 
 * @param heap A 1-indexed array that contains the root to sift and its subtrees.
 * @param n The size of the binary tree.
 * @param root The index of the value to sift down. Its left and right subtrees must be heapified.
**/
static void repair_down(T heap[], size_t const n, size_t const root) {
    T const root_value = heap[root];
    size_t father = root, son;
    while ((son = father * 2) <= n) {  // left son
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
 * @brief This restores the heap order, given that it is violated at at most one position.
 * @note Funfact: The parameter name `wo` is German and means `where`.
 * For some reason, the name bugged me (Z.A. Weil) back in the summer of 2019
 * as student of the class ‘Datenstrukturen’ (Data Structures), where it was used
 * in pseudo code on HeapSort, and it bugged me again as tutor of the class
 * ‘Algorithmen und Datenstrukturen 1’ (need I translate?) in the summer of 2024.
 * I hereby immortalised it, even if `wo` should perish from the courses’ script
 * when its long overdue rewrite happens.
 * 
 * @param heap The 1-indexed heap where the order may be violated.
 * @param wo The index of the vertex where the heap order may be violated.
**/
static void repair_up(T heap[], size_t wo) {
    T const p = heap[wo];
    while (heap[wo/2] < p) {
        heap[wo] = heap[wo/2];
        wo /= 2;
    }
    heap[wo] = p;
}

/**
 * @brief Removes and returns the root of a heap.
 * 
 * @param heap The 1-indexed heap whose root is to be returned.
 * @param n The length of the heap.
 * 
 * @return The original root value.
**/
static T extract_root(T heap[], size_t const n) {
    T const root_value = heap[1];
    size_t father = 1, son;
    /* Move hole down. */
    // Do a repair-down that moves the ‘hole’ at the root down to the last or erelast layer.
    while ((son = father * 2) <= n) {  // left son
        if (heap[son + 1] > heap[son]) {  // Check if right son is bigger.
            heap[father] = heap[son + 1];  // Shift right son up.
            father = son + 1;
        } else {
            heap[father] = heap[son];  // Shift left son up.
            father = son;
        }
    }
    /* Move right-most leaf in bottom layer into hole. */
    heap[father] = heap[n];
    /* Repair the heap structure. */
    repair_up(heap, father);
    return root_value;
}

/**
 * @brief Removes and returns the root of a heap.
 * Does the same number of swaps as the regular `repair_down`.
 * 
 * @param heap The 1-indexed heap whose root is to be returned.
 * @param n The length of the heap.
 * @return 
**/
static T extract_root_swap_parity(T heap[], size_t const n) {
    T const root_value = heap[1];
    size_t father = 1, son;
    /* Trace downwards motion of hole. */
    while ((son = father * 2) <= n) {  // left son
        if (heap[son + 1] > heap[son]) {  // Check if right son is bigger.
            father = son + 1;
        } else {
            father = son;
        }
    }
    /* Go whither `repair_up` would put the hole. */
    while (heap[father] < heap[n])
        father /= 2;
    /* Move rightmost leaf in bottom layer. */
    T to_sift_up = heap[father];
    heap[father] = heap[n];
    /* Move the leaf's predecessors upwards. */
    for (size_t wo = father / 2; wo >= 1; wo /= 2) {
        T const tmp = heap[wo];
        heap[wo] = to_sift_up;
        to_sift_up = tmp;
    }
    // /* Move the leaf's predecessors upwards. (unrolled) */
    // size_t wo = father;
    // while (wo / 4 >= 1) {
    //     T const tmp = heap[wo / 4];
    //     heap[wo / 4] = heap[wo / 2];
    //     heap[wo / 2] = to_sift_up;
    //     to_sift_up = tmp;
    //     wo /= 4;
    // }
    // if (wo == 2 || wo == 3) {
    //     heap[1] = to_sift_up;
    // }
    return root_value;
}

/**
 * @brief An implementation of standard HeapSort.
 * It only sifts down.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void heap_sort_only_down(T * const start, T * const end) {
    size_t n = end - start + 1;
    T * const heap = start - 1;
    /* Build a heap using Floyd’s method. */
    // if (!(n & 1))  // For some reason, this if-statement worsens the runtime drastically.
        heap[n + 1] = T_MIN;  // If `n' is even, the last leaf is a left one.
    for (size_t r = n / 2; r > 0; r--) {
        repair_down(heap, n, r);
    }
    /* Sort by repeatedly putting the root at the end of the heap. */
    if (!(n & 1)) {  // If `n' is even, the last leaf is a left one. (cf. loop below)
        swap(&heap[1], &heap[n--]);
        repair_down(heap, n, 1);
    }
    // `i` is always odd. When there is an odd number of elements, the last leaf is a right one.
    // Pulling it to the front raises the need for a sentinel value,
    // since the leaf with which one ends up at the end of `repair_down` may be the last leaf,
    // which is now a left one. Since the right brothers of nodes are checked,
    // a sentinel value erases the need for an additional bounds check.
    size_t i;
    for (i = n; i > HEAP_TO_INSERTION; i -= 2) {
        T const biggest_element = heap[1];
        heap[1] = heap[i];
        heap[i] = T_MIN;
        repair_down(heap, i - 1, 1);
        heap[i] = biggest_element;

        swap(&heap[1], &heap[i - 1]);
        repair_down(heap, i - 2, 1);
    }
#if (HEAP_TO_INSERTION > 2)
    insertion_sort_sentinel(&heap[1], &heap[i]);
#endif  // HEAP_TO_INSERTION > 2
}

/**
 * @brief An implementation of standard HeapSort.
 * It sifts both up and down.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void heap_sort_both_up_and_down(T * const start, T * const end) {
    size_t n = end - start + 1;
    T * const heap = start - 1;
    T const prev_value = heap[0];
    heap[0] = T_MAX;  // sentinel value for `repair_up`
    /* Build a heap using Floyd’s method. */
    // if (!(n & 1))  // For some reason, this if-statement worsens the runtime drastically.
        heap[n + 1] = T_MIN;  // If `n' is even, the last leaf is a left one.
    for (size_t r = n / 2; r > 0; r--) {
        repair_down(heap, n, r);
    }
    /* Sort by repeatedly putting the root at the end of the heap. */
    if (!(n & 1)) {  // If `n' is even, the last leaf is a left one. (cf. loop below)
        T const biggest_element = extract_root(heap, n);
        heap[n--] = biggest_element;
    }
    // `i` is always odd. When there is an odd number of elements, the last leaf is a right one.
    // Placing it in the hole raises the need for a sentinel value,
    // since the leaf with which one ends up at the end of `extract_root` may be the last leaf,
    // which is now a left one. Since the right brothers of nodes are checked,
    // a sentinel value erases the need for an additional bounds check.
    size_t i;
    for (i = n; i > HEAP_TO_INSERTION; i -= 2) {
        T const biggest_element = extract_root(heap, i);

        heap[i] = T_MIN;  // The last leaf is now a left one, so a sentinel value is needed.
        T const second_biggest_element = extract_root(heap, i - 1);

        heap[i] = biggest_element;
        heap[i - 1] = second_biggest_element;
    }
    heap[0] = prev_value;
#if (HEAP_TO_INSERTION > 2)
    insertion_sort_sentinel(&heap[1], &heap[i]);
#endif  // HEAP_TO_INSERTION > 2
}

/**
 * @brief An implementation of standard HeapSort.
 * It sifts both up and down while doing the same number of swaps as `heap_sort_only_down`.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void heap_sort_both_up_and_down_swap_parity(T * const start, T * const end) {
    size_t n = end - start + 1;
    T * const heap = start - 1;
    T const prev_value = heap[0];
    heap[0] = T_MAX;  // sentinel value for `repair_up`
    /* Build a heap using Floyd’s method. */
    // if (!(n & 1))  // For some reason, this if-statement worsens the runtime drastically.
        heap[n + 1] = T_MIN;  // If `n' is even, the last leaf is a left one.
    for (size_t r = n / 2; r > 0; r--) {
        repair_down(heap, n, r);
    }
    /* Sort by repeatedly putting the root at the end of the heap. */
    if (!(n & 1)) {  // If `n' is even, the last leaf is a left one. (cf. loop below)
        T const biggest_element = extract_root_swap_parity(heap, n);
        heap[n--] = biggest_element;
    }
    // `i` is always odd. When there is an odd number of elements, the last leaf is a right one.
    // Placing it in the hole raises the need for a sentinel value,
    // since the leaf with which one ends up at the end of `extract_root` may be the last leaf,
    // which is now a left one. Since the right brothers of nodes are checked,
    // a sentinel value erases the need for an additional bounds check.
    size_t i;
    for (i = n; i > HEAP_TO_INSERTION; i -= 2) {
        T const biggest_element = extract_root_swap_parity(heap, i);

        heap[i] = T_MIN;  // The last leaf is now a left one, so a sentinel value is needed.
        T const second_biggest_element = extract_root_swap_parity(heap, i - 1);

        heap[i] = biggest_element;
        heap[i - 1] = second_biggest_element;
    }
    heap[0] = prev_value;
#if (HEAP_TO_INSERTION > 2)
    insertion_sort_sentinel(&heap[1], &heap[i]);
#endif  // HEAP_TO_INSERTION > 2
}

// Creating the starting runs for MergeSort.
#define CREATE_STARTING_RUNS()                                                           \
if (end - start + 1 <= MERGE_THRESHOLD) {                                                \
    shell_sort(start, end);                                                              \
    flags[me()] = false;                                                                 \
    return;                                                                              \
}                                                                                        \
shell_sort(start, start + MERGE_THRESHOLD - 1);                                          \
for (T *t = start + MERGE_THRESHOLD; t < end; t += MERGE_THRESHOLD) {                    \
    T before_sentinel[FIRST_STEP];                                                       \
    _Pragma("unroll")  /* Set sentinel values. */                                        \
    for (size_t i = 0; i < FIRST_STEP; i++) {                                            \
        before_sentinel[i] = *(t - i - 1);                                               \
        *(t - i - 1) = T_MIN;                                                            \
    }                                                                                    \
    T * const run_end = (t + MERGE_THRESHOLD - 1 > end) ? end : t + MERGE_THRESHOLD - 1; \
    shell_sort(t, run_end);                                                              \
    _Pragma("unroll")  /* Restore old values. */                                         \
    for (size_t i = 0; i < FIRST_STEP; i++) {                                            \
        *(t - i - 1) = before_sentinel[i];                                               \
    }                                                                                    \
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
 * The copying is done using batches of length `MERGE_THRESHOLD`:
 * If there are at least `MERGE_THRESHOLD` many elements to copy, they are copied at once.
 * This way, the loop overhead is cut by about a `MERGE_THRESHOLD`th in good cases.
 * 
 * @param in The first element to copy.
 * @param until The last element to copy.
 * @param out Whither to place the first element to copy.
**/
static inline void flush_starting_run(T *in, T *until, T *out) {
    while (in + MERGE_THRESHOLD - 1 <= until) {
        #pragma unroll
        for (size_t k = 0; k < MERGE_THRESHOLD; k++)
            *(out + k) = *(in + k);
        out += MERGE_THRESHOLD;
        in += MERGE_THRESHOLD;
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
    if (*(start_2 - 1) <= *(end_2)) {
        while (true) {
            if (*i <= *j) {
                *out++ = *i++;
                if (i == start_2) {  // Pulling these if-statements out of the loop …
                    flush_batch(j, end_2, out);
                    return;
                }
            } else {
                *out++ = *j++;
            }
        }
    } else {
        while (true) {
            if (*i <= *j) {
                *out++ = *i++;
            } else {
                *out++ = *j++;
                if (j > end_2) {  // … worsens the runtime.
                    flush_batch(i, start_2 - 1, out);
                    return;
                }
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
    for (size_t run_length = MERGE_THRESHOLD; run_length < n; run_length *= 2) {
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
    if (*end_1 < *end_2) {
        while (true) {
            if (*i < *j) {
                *out++ = *i++;
                if (i > end_1) {
                    flush_batch(j, end_2, out);
                    return;
                }
            } else {
                *out++ = *j++;
            }
        }
    } else {
        while (true) {
            if (*i < *j) {
                *out++ = *i++;
            } else {
                *out++ = *j++;
                if (j > end_2) {
                    return;
                }
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
    for (size_t run_length = MERGE_THRESHOLD; run_length < n; run_length *= 2) {
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
                for (size_t k = 0; k < MERGE_THRESHOLD; k++)
                    *(out + k) = *(j + k);
                out += MERGE_THRESHOLD;
                j += MERGE_THRESHOLD;
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
//     for (size_t run_length = MERGE_THRESHOLD; run_length < n; run_length *= 2) {
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
    {{ "HeapOnlyDown", heap_sort_only_down }},
    {{ "HeapUpDown", heap_sort_both_up_and_down }},
    {{ "HeapSwapParity", heap_sort_both_up_and_down_swap_parity }},
    {{ "Merge", merge_sort_no_write_back}},
    {{ "MergeWriteBack", merge_sort_write_back }},
    {{ "MergeHalfSpace", merge_sort_half_space }},
};
// size_t __host lengths[] = { 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072 };
size_t __host lengths[] = { 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024 };
// size_t __host lengths[] = {  // for `MERGE_THRESHOLD` <= 15
//     MERGE_THRESHOLD, MERGE_THRESHOLD + 1, 4*MERGE_THRESHOLD + 1, 16*MERGE_THRESHOLD + 1,
//     64*MERGE_THRESHOLD + 1, 128*MERGE_THRESHOLD + 1,
// };
// size_t __host lengths[] = {  // for `MERGE_THRESHOLD` < 64
//     MERGE_THRESHOLD, MERGE_THRESHOLD + 1, 4*MERGE_THRESHOLD + 1, 16*MERGE_THRESHOLD + 1,
//     64*MERGE_THRESHOLD + 1,
// };
// size_t __host lengths[] = {  // for `MERGE_THRESHOLD` > 64
//     MERGE_THRESHOLD, MERGE_THRESHOLD + 1, 4*MERGE_THRESHOLD + 1, 16*MERGE_THRESHOLD + 1,
// };
// size_t __host lengths[] = {  // for `MERGE_THRESHOLD` == 16 with both worst-case and best-caseg
//      1*MERGE_THRESHOLD,  1*MERGE_THRESHOLD + 1,  2*MERGE_THRESHOLD,  2*MERGE_THRESHOLD + 1,
//      4*MERGE_THRESHOLD,  4*MERGE_THRESHOLD + 1,  8*MERGE_THRESHOLD,  8*MERGE_THRESHOLD + 1,
//     16*MERGE_THRESHOLD, 16*MERGE_THRESHOLD + 1, 32*MERGE_THRESHOLD, 32*MERGE_THRESHOLD + 1,
//     64*MERGE_THRESHOLD, 64*MERGE_THRESHOLD + 1,
// };
size_t __host num_of_algos = sizeof algos / sizeof algos[0];
size_t __host num_of_lengths = sizeof lengths / sizeof lengths[0];

int main(void) {
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
