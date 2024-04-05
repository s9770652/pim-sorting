#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <defs.h>
#include <perfcounter.h>

#include "base_sorts.h"
#include "mram_loop.h"
#include "random_distribution.h"

// the input length at which QuickSort changes to InsertionSort
#define QUICK_TO_INSERTION (96)

typedef void base_sort_algo(T *, T *);
struct algos_to_test {
    base_sort_algo *algo;
    char name[14];
};

/**
 * @brief An implementation of standard InsertionSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
**/
static void insertion_sort_nosentinel(T *start, T * const end) {
    T *curr, * const start_orig = start;
    while ((curr = start++) < end) {  // todo: `++start` is slower‽
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (prev >= start_orig && *prev > to_sort) {
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
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
**/
static void insertion_sort_sentinel(T *start, T * const end) {
    T *curr;
    while ((curr = start++) < end) {  // todo: `++start` is slower‽
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of InsertionSort where compared elements need not be neighbours.
 * Needed by ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_steps(T *start, T * const end, size_t const step) {
    T *curr, * const start_orig = start;
    while ((curr = (start += step)) < end) {
        T *prev = curr - step;  // todo: What if negative‽
        T const to_sort = *curr;
        while (prev >= start_orig && *prev > to_sort) {
            *curr = *prev;
            curr = prev;
            prev -= step;
        }
        *curr = to_sort;
    }
}

/**
 * @brief A combination of `insertion_sort_sentinel` and `insertion_sort_with_steps`.
 * Needed by ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 * @param step The distance between any two elements compared.
**/
static void insertion_sort_with_steps_sentinel(T *start, T * const end, size_t const step) {
    T *curr;
    while ((curr = (start += step)) < end) {
        T *prev = curr - step;  // todo: What if negative‽
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
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
**/
static void shell_sort_classic(T * const start, T * const end) {
    // Sort all elements which are n/2, n/4, …, 4, 2 indices apart.
    size_t const n = end - start;
    for (size_t inc = n / 2; inc >= 2; inc /= 2)
        for (size_t j = 0; j < inc; j++)
            insertion_sort_with_steps(&start[j], end, inc);
    insertion_sort_sentinel(start, end);
}

// Ciura
static void shell_sort_ciura(T * const start, T * const end) {
    size_t const steps[] = { 301, 132, 57, 23, 10, 4 };
    for (size_t i = 0; i < 4; i++)
        for (size_t j = 0; j < steps[i]; j++)
            insertion_sort_with_steps(&start[j], end, steps[i]);
    for (size_t i = 4; i < 6; i++)
        for (size_t j = 0; j < steps[i]; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, steps[i]);
    insertion_sort_sentinel(start, end);
}

// Ciura-like
static void shell_sort_custom(T * const start, T * const end) {
    size_t const step = 6;
    if (end - start <= 16)
        for (size_t j = 0; j < step; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, step);
    insertion_sort_sentinel(start, end);
}

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
 * The method of choosing currently must be changed by (un-)commenting the respective code lines.
 * Possible are:
 * - always the leftmost element
 * - the mean of the leftmost and the rightmost element
 * - the median of the leftmost, middle and rightmost element
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (exclusive) end of said array.
 *
 * @return The pivot element.
**/
static inline T get_pivot(T const * const start, T const * const end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
    /* Always the leftmost element. */
    // return *start;
    /* The mean of the leftmost and the rightmost element. */
    return (*start + *(end-1)) / 2;
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
    if (end - start <= QUICK_TO_INSERTION) {  // false if `end < start` due to wrapping
        // insertion_sort_sentinel(start, end);
        shell_sort_custom(start, end);
        return;
    } else if (end <= start) return;
    /* Put elements into respective partitions. */
    T *i = start, *j = end;
    T pivot = get_pivot(start, end);
    do {
        while (*i < pivot) i++;
        while (*j > pivot) j--;

        if (i <= j)
            swap(i++, j--);
    } while (i <= j);
    /* Sort left and right partitions. */
    quick_sort_recursive(start, j);
    quick_sort_recursive(i, end);
}

static void quick_sort_iterative(T * const start, T * const end) {
    // A “call” stack for holding the values of `left` and `right` is maintained.
    // Since QuickSort works in-place, it is stored right after the end.
    T **stack = (T **)(uintptr_t)end;
    *++stack = start;
    *++stack = end;
    do {
        T *right = *stack--, *left = *stack--;  // Pop from stack.
        /* Detect base cases. */
        if (right - left <= QUICK_TO_INSERTION) {  // false if `right < left` due to wrapping
            insertion_sort_sentinel(left, right);
            continue;
        } else if (right <= left) return;
        /* Put elements into respective partitions. */
        T *i = left, *j = right;
        T pivot = get_pivot(left, right);
        do {
            while (*i < pivot) i++;
            while (*j > pivot) j--;

            if (i <= j)
                swap(i++, j--);
        } while (i <= j);
        /* Put left partition on stack. */
        if (j > left) {
            *++stack = left;
            *++stack = j;
        }
        /* Put right partition on stack. */
        if (right > i) {
            *++stack = i;
            *++stack = right;
        }
    } while (stack != (T **)(uintptr_t)end);
}

/**
 * @brief An approximation of the square root using the Babylonian method.
 * This function is needed as `math.h` is not provided for DPUs.
 * 
 * @param square The number of which to take the root.
 * 
 * @return The approximation of the square root after 16 iterations.
**/
static float sqroot_on_dpu(float const square) {
    float root = square / 3, prev_root;
    if (square <= 0) return 0;
    do {
        prev_root = root;
        root = (root + square / root) / 2;
    } while (root - prev_root > 1 || root - prev_root < -1 );
    return root;
}

/**
 * @brief The arithmetic mean of measured times.
 * 
 * @param zeroth The zeroth moment, that is, the number of measurements.
 * @param first The first moment, that is, the sum of measured times.
 * 
 * @return The mean time.
**/
static perfcounter_t get_mean_of_time(perfcounter_t zeroth, perfcounter_t first) {
    return first / zeroth;
}

/**
 * @brief The standard deviation of measured times.
 * 
 * @param zeroth The zeroth moment, that is, the number of measurements.
 * @param first The first moment, that is, the sum of measured times.
 * @param second The second moment, that is, the sum of squared measured times.
 * 
 * @return The standard deviation.
**/
static perfcounter_t get_std_of_time(perfcounter_t zeroth, perfcounter_t first, perfcounter_t second) {
    return sqroot_on_dpu((zeroth * second - first * first) / (zeroth * (zeroth - 1)));
}

#ifndef PRINT_IN_FILE_FRIENDLY_FORMAT
#define PRINT_IN_FILE_FRIENDLY_FORMAT (0)
#endif
/**
 * @brief Prints the average of the measured runtimes and their standard deviation to the console.
 * Additionally prints the average on a per-element basis.
 * 
 * @param algos A list of sorting algorithms measured.
 * @param num_of_algos The number of sorting algorithms measured.
 * @param length The number of input elements which were sorted.
 * @param zeroth The zeroth moment (universal for all algorithms).
 * @param firsts The measured first moments of all algorithms.
 * @param seconds The measured second moments of all algorithms.
**/
static void print_measurements(struct algos_to_test const * const algos, size_t const num_of_algos,
        size_t const length, perfcounter_t const zeroth, perfcounter_t const * const firsts,
        perfcounter_t const * const seconds) {
#if !PRINT_IN_FILE_FRIENDLY_FORMAT
    size_t const log = 31 - __builtin_clz(length);
    printf("Length: %zd\n", length);
    for (size_t id = 0; id < num_of_algos; id++) {
        perfcounter_t mean = get_mean_of_time(zeroth, firsts[id]);
        perfcounter_t std = get_std_of_time(zeroth, firsts[id], seconds[id]);
        printf(
            "%-14s: %8lu ±%6lu C \t %5lu C/n \t %5lu C/(n log n) \t %5lu C/n²\n",
            algos[id].name,
            mean,
            std,
            mean / length,
            mean / (length * log),
            mean / (length * length)
        );
    }
    printf("\n");
#endif
}

void test_wram_sorts(triple_buffers *buffers, struct dpu_arguments *args) {
    if (me() != 0) return;
    T *cache = buffers->cache;

    for (int i = 0; i < 16; i++)  // todo: remove after proper padding implemented in buffers.c
        cache[i] = T_MIN;
    cache += 16;

    size_t const max_length = (TRIPLE_BUFFER_SIZE > args->length) ? args->length : TRIPLE_BUFFER_SIZE;
    struct algos_to_test const algos[] = {
        // { insertion_sort_nosentinel, "Insert" },
        { insertion_sort_sentinel, "Insert (Sent.)" },
        // { shell_sort_classic, "Shell" },
        // { shell_sort_ciura, "Shell (Ciura)" },
        // { shell_sort_custom, "Shell (Custom)" },
        // { quick_sort_recursive, "Quick (Rec.)" },
        // { quick_sort_iterative, "Quick (It.)" }
    };
    enum { num_of_algos = sizeof algos / sizeof algos[0] };
    perfcounter_t first_moments[num_of_algos], second_moments[num_of_algos], curr_time;

    /* Do warm-up repetitions. */
    for (uint32_t rep = 0; rep < args->n_warmup; rep++) {
        generate_uniform_distribution_wram(&cache[0], &cache[max_length], args->upper_bound);
        insertion_sort_sentinel(&cache[0], &cache[max_length]);
    }

    /* Do and time actual repetitions. */
    size_t const lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024 };
    assert(lengths[sizeof(lengths) / sizeof(lengths[0]) - 1] <= (TRIPLE_BUFFER_SIZE >> DIV));

    printf("TEST: BASE SORTING ALGORITHMS (C: cycles, n: length)\n\n");
    for (size_t li = 0; li < (sizeof lengths / sizeof lengths[0]); li++) {
        memset(first_moments, 0, sizeof first_moments);
        memset(second_moments, 0, sizeof second_moments);
        size_t const length = lengths[li];
        T * const start = cache, * const end = &cache[length];

        for (uint32_t rep = 0; rep < args->n_reps; rep++) {
            for (size_t id = 0; id < num_of_algos; id++) {
                generate_uniform_distribution_wram(start, end, args->upper_bound);
                curr_time = perfcounter_get();
                algos[id].algo(start, end);
                curr_time = perfcounter_get() - curr_time;
                first_moments[id] += curr_time;
                second_moments[id] += curr_time * curr_time;
#if CHECK_SANITY
                T smallest = cache[0];
                for (size_t i = 1; i < length; i++) {
                    if (cache[i] < smallest) {
                        printf("%s: Not sorted!\n", algos[id].name);
                        break;
                    }
                    smallest = cache[i];
                }
#endif
            }
        }

        print_measurements(algos, num_of_algos, length, args->n_reps, first_moments, second_moments);
    }
}

void test_custom_shell_sorts(triple_buffers *buffers, struct dpu_arguments *args) {
    if (me() != 0) return;
    T *cache = buffers->cache;
    enum { max_step = 11 };
    perfcounter_t first_moments[max_step], second_moments[max_step], curr_time;

    /* Add additional sentinel values. */
    for (size_t i = 0; i < max_step; i++)
        cache[i] = T_MIN;
    cache += max_step;

    /* Create list of ShellSort names. */
    struct algos_to_test algos[] = {  // no sprintf \[T]/
        { NULL, "1" }, { NULL, "2" }, { NULL, "3" }, { NULL, "4" }, { NULL, "5" }, { NULL, "6" },
        { NULL, "7" }, { NULL, "8" }, { NULL, "9" }, { NULL, "10" }, { NULL, "11" }
    };
    assert(max_step <= (sizeof algos / sizeof algos[0]));

    /* Do and time actual repetitions. */
    size_t const lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128 };
    assert(lengths[sizeof(lengths) / sizeof(lengths[0]) - 1] + max_step <= (TRIPLE_BUFFER_SIZE >> DIV));

    printf("TEST: CUSTOM SHELLSORTS (C: cycles, n: length)\n\n");
    for (size_t li = 0; li < (sizeof lengths / sizeof lengths[0]); li++) {
        memset(first_moments, 0, sizeof first_moments);
        memset(second_moments, 0, sizeof second_moments);
        size_t const length = lengths[li];
        T * const start = cache, * const end = &cache[length];

        for (uint32_t rep = 0; rep < args->n_reps; rep++) {
            /* InsertionSort */
            generate_uniform_distribution_wram(start, end, args->upper_bound);
            curr_time = perfcounter_get();
            insertion_sort_sentinel(start, end);
            curr_time = perfcounter_get() - curr_time;
            first_moments[0] += curr_time;
            second_moments[0] += curr_time * curr_time;
            /* ShellSorts */
            for (size_t step = 2; step <= max_step; step++) {
                generate_uniform_distribution_wram(start, end, args->upper_bound);
                curr_time = perfcounter_get();
                for (size_t j = 0; j < step; j++)
                    insertion_sort_with_steps_sentinel(start, end, step);
                insertion_sort_sentinel(start, end);
                curr_time = perfcounter_get() - curr_time;
                first_moments[step-1] += curr_time;
                second_moments[step-1] += curr_time * curr_time;
            }
        }

        print_measurements(algos, max_step, length, args->n_reps, first_moments, second_moments);
    }
}
