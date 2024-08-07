/**
 * @file
 * @brief Measures runtimes of sorting algorithms intended for small input arrays.
 * 
 * The tested algorithms are:
 * - InsertionSort
 * - ShellSort
 * - BubbleSort
 * - SelectionSort
 * Whether the ShellSorts have two rounds or three, depends on the value of `BIG_STEP`.
 * See their respective documentation for more information.
**/

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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

#define BIG_STEP (0)

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
 * It also does not sort already sorted tails.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void bubble_sort_adaptive(T * const start, T * const end) {
    T *until = end, *swapped;
    do {
        swapped = NULL;
        for (T *i = start; i < until; i++) {
            if (*i > *(i + 1)) {
                swap(i, i + 1);
                swapped = i;
            }
        }
        until = swapped;
    } while (swapped);
}

/**
 * @brief An implementation of standard SelectionSort.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void selection_sort(T * const start, T * const end) {
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
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void insertion_sort_nosentinel(T * const start, T * const end) {
    T *curr, *i = start + 1;
    while ((curr = i++) <= end) {
        T const to_sort = *curr;
        while (curr > start && *(curr - 1) > to_sort) {  // `(curr - 1) > start` is slower.
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
        T const to_sort = *curr;
        while (*(curr - step) > to_sort) {  // `-step` always valid due to the sentinel value
            *curr = *(curr - step);
            curr -= step;
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of InsertionSort which needs no predefined sentinel value.
 * Instead if the current element is the smallest known one, it is moved to the front immediately.
 * If not, the current first element must be bigger and thus is a sentinel value.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static void insertion_sort_implicit_sentinel(T * const start, T * const end) {
    T *curr, *i = start + 1;
    while ((curr = i++) <= end) {
        T const to_sort = *curr;
        if (*curr < *start) {  // Is the current element the new minimum?
            while (curr > start) {  // Move all previous elements backwards.
                *curr = *(curr - 1);
                curr--;
            }
            *start = to_sort;  // Place the new minimum at the start.
        } else {  // Otherwise, do a regular InsertionSort.
            while (*(curr - 1) > to_sort) {
                *curr = *(curr - 1);
                curr--;
            }
            *curr = to_sort;
        }
    }
}

/**
 * @brief An implementation of InsertionSort fully in inline assembler.
 * It is the fastest version of all but also the most fragile.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static __attribute__((unused)) void insertion_sort_assembly(T *const start, T * const end) {
    (void)start, (void)end;
    __asm__(
        "\tadd %[i], %[i], 4\n"
        "\tjleu %[i], %[end], 1f\n"
    "5:\n"
        "\tjump r23\n"
    "4:\n"
        "\tadd %[i], %[i], 4\n"
        "\tsw r3, 0, r2\n"
        "\tjgtu %[i], %[end], 5b\n"
    "1:\n"
        "\tlw r2, %[i], 0\n"
        "\tlw r4, %[i], -4\n"
        "\tmove r3, %[i]\n"
        "\tjleu r4, r2, 4b\n"
        // "\tmove r3, %[i]\n"  // Present in the normal compilation. Why?
    "3:\n"
        "\tsw r3, 0, r4\n"
        "\tlw r4, r3, -8\n"
        "\tadd r3, r3, -4\n"
        "\tjgtu r4, r2, 3b\n"
        "\tjump 4b"
        :
        : [i] "r"(start), [end] "r"(end)
    );
}

/**
 * @brief An implementation of ShellSort using Ciura’s optimal sequence for 128 elements.
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
**/
static __attribute__((unused)) void shell_sort_ciura(T * const start, T * const end) {
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

/**
 * @brief Creates a ShellSort of the name `shell_sort_custom_step_x`
 * with `x` ∈ {2, …, 9} being the step size before the final InsertionSort.
 * If `BIG_STEP` is bigger than `x`, there will be three rounds
 * with `BIG_STEP` being the first step size.
 * 
 * @param step The step size before the final InsertionSort. [Parameter of the macro]
 * @param start The first element of the WRAM array to sort. [Parameter of the ShellSort]
 * @param end The last element of said array. [Parameter of the ShellSort]
**/
#define SHELL_SORT_CUSTOM_STEP_X(step)                                      \
static void shell_sort_custom_step_##step(T * const start, T * const end) { \
    if (BIG_STEP > step) {                                                  \
        _Pragma("nounroll")  /* The .text region may overflow otherwise. */ \
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

static __attribute__((unused)) void empty_sort(T *start, T *end) { (void)start; (void)end; }

union algo_to_test __host algos[] = {
    // {{ "Empty", empty_sort }},
    {{ "1", insertion_sort_sentinel }},
    {{ "2", shell_sort_custom_step_2 }},
    {{ "3", shell_sort_custom_step_3 }},
    {{ "4", shell_sort_custom_step_4 }},
    {{ "5", shell_sort_custom_step_5 }},
    {{ "6", shell_sort_custom_step_6 }},
    {{ "7", shell_sort_custom_step_7 }},
    {{ "8", shell_sort_custom_step_8 }},
    {{ "9", shell_sort_custom_step_9 }},
    // {{ "Ciura", shell_sort_ciura }},
    {{ "1NoSentinel", insertion_sort_nosentinel }},
    {{ "1Implicit", insertion_sort_implicit_sentinel }},
    // {{ "1Assembly", insertion_sort_assembly }},
    {{ "BubbleAdapt", bubble_sort_adaptive }},
    {{ "BubbleNonAdapt", bubble_sort_nonadaptive }},
    {{ "Selection", selection_sort }},
};
size_t __host lengths[] = {  // for all small sorts
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
};
// size_t __host lengths[] = {  // for two-tier vs. three-tier ShellSort
//     16, 24, 32, 48, 64, 96, 128
// };
// size_t __host lengths[] = {  // for Ciura’s ShellSort
//     16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024
// };
size_t __host num_of_algos = sizeof algos / sizeof algos[0];
size_t __host num_of_lengths = sizeof lengths / sizeof lengths[0];

int main(void) {
    if (me() != 0) return EXIT_SUCCESS;
    (void)call_stacks;

    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
        /* Add additional sentinel values. */
        size_t num_of_sentinels = 16;  // 17 is the maximum step, 1 is already present.
        for (size_t i = 0; i < num_of_sentinels; i++)
            buffers[me()].cache[i] = T_MIN;
        buffers[me()].cache += num_of_sentinels;
        assert(lengths[num_of_lengths - 1] + num_of_sentinels <= (TRIPLE_BUFFER_SIZE >> DIV));
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
