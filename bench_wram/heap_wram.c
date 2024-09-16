/**
 * @file
 * @brief Measuring runtimes of HeapSorts (sequential, WRAM).
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <alloc.h>
#include <defs.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "buffers.h"
#include "checkers.h"
#include "common.h"
#include "communication.h"
#include "pivot.h"
#include "random_distribution.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot

// The input length at which HeapSort changes to InsertionSort.
#define HEAP_THRESHOLD (15)
static_assert(HEAP_THRESHOLD & 1, "Applying to right sons, HEAP_THRESHOLD should be odd!");

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
    for (i = n; i > HEAP_THRESHOLD; i -= 2) {
        T const biggest_element = heap[1];
        heap[1] = heap[i];
        heap[i] = T_MIN;
        repair_down(heap, i - 1, 1);
        heap[i] = biggest_element;

        swap(&heap[1], &heap[i - 1]);
        repair_down(heap, i - 2, 1);
    }
#if (HEAP_THRESHOLD > 2)
    insertion_sort_sentinel(&heap[1], &heap[i]);
#endif  // HEAP_THRESHOLD > 2
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
    for (i = n; i > HEAP_THRESHOLD; i -= 2) {
        T const biggest_element = extract_root(heap, i);

        heap[i] = T_MIN;  // The last leaf is now a left one, so a sentinel value is needed.
        T const second_biggest_element = extract_root(heap, i - 1);

        heap[i] = biggest_element;
        heap[i - 1] = second_biggest_element;
    }
    heap[0] = prev_value;
#if (HEAP_THRESHOLD > 2)
    insertion_sort_sentinel(&heap[1], &heap[i]);
#endif  // HEAP_THRESHOLD > 2
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
    for (i = n; i > HEAP_THRESHOLD; i -= 2) {
        T const biggest_element = extract_root_swap_parity(heap, i);

        heap[i] = T_MIN;  // The last leaf is now a left one, so a sentinel value is needed.
        T const second_biggest_element = extract_root_swap_parity(heap, i - 1);

        heap[i] = biggest_element;
        heap[i - 1] = second_biggest_element;
    }
    heap[0] = prev_value;
#if (HEAP_THRESHOLD > 2)
    insertion_sort_sentinel(&heap[1], &heap[i]);
#endif  // HEAP_THRESHOLD > 2
}

union algo_to_test __host algos[] = {
    {{ "HeapOnlyDown", { .wram = heap_sort_only_down } }},
    {{ "HeapUpDown", { .wram = heap_sort_both_up_and_down } }},
    {{ "HeapSwapParity", { .wram = heap_sort_both_up_and_down_swap_parity } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
    if (me() != 0) return EXIT_SUCCESS;

    /* Set up buffers. */
    assert(host_to_dpu.length <= TRIPLE_BUFFER_LENGTH);
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
        buffers[me()].cache[SENTINELS_NUMS - 1] = T_MIN;
    }
    T * const cache = buffers[me()].cache + SENTINELS_NUMS;

    /* Set up dummy values if called via debugger. */
    if (host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 128;
        host_to_dpu.offset = DMA_ALIGNED(host_to_dpu.length * sizeof(T)) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
    }

    /* Perform test. */
    T __mram_ptr *read_from = input;
    T * const start = cache, * const end = &cache[host_to_dpu.length - 1];
    unsigned int const transfer_size = DMA_ALIGNED(sizeof(T[host_to_dpu.length]));
    sort_algo_wram * const algo = algos[host_to_dpu.algo_index].data.fct.wram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());
        mram_read_triple(read_from, cache, transfer_size);

        array_stats stats_before;
        get_stats_unsorted_wram(cache, host_to_dpu.length, &stats_before);

        perfcounter_config(COUNT_CYCLES, true);
        dpu_time new_time = perfcounter_get();
        algo(start, end);
        new_time = perfcounter_get() - new_time - CALL_OVERHEAD_CYCLES;
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
