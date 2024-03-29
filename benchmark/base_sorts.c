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

extern T __mram_ptr input[];

typedef void (base_sort_algo)(T *, T *);

/**
 * @brief An implementation of standard InsertionSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (inclusive) end of said array.
**/
static void insertion_sort_nosentinel(T *start, T * const end) {
    T *curr, * const start_orig = start;
    while ((curr = start++) <= end) {  // todo: `++start` is slower‽
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
 * @param end The (inclusive) end of said array.
**/
static void insertion_sort_sentinel(T *start, T * const end) {
    T *curr;
    while ((curr = start++) <= end) {  // todo: `++start` is slower‽
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
 * Does not use sentinel values because,
 * for big arrays, literally one third of the cache would be filled with them.
 * Needed by ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (inclusive) end of said array.
 * @param inc The distance between any two elements compared.
**/
static void insertion_sort_with_steps(T *start, T * const end, size_t inc) {
    T *curr, * const start_orig = start;
    while ((curr = (start += inc)) <= end) {
        T *prev = curr - inc;  // todo: What if negative‽
        T const to_sort = *curr;
        while (prev >= start_orig && *prev > to_sort) {
            *curr = *prev;
            curr = prev;
            prev -= inc;
        }
        *curr = to_sort;
    }
}

static void insertion_sort_with_steps_sentinel(T *start, T * const end, size_t inc) {
    T *curr;
    while ((curr = (start += inc)) <= end) {
        T *prev = curr - inc;  // todo: What if negative‽
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev;
            prev -= inc;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

/**
 * @brief An implementation of standard ShellSort.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (inclusive) end of said array.
**/
static void shell_sort_classic(T * const start, T * const end) {
    // Sort all elements which are n/2, n/4, …, 4, 2 indices apart.
    size_t const n = end - start + 1;
    for (size_t inc = n / 2; inc >= 2; inc /= 2)
        for (size_t j = 0; j < inc; j++)
            insertion_sort_with_steps(&start[j], end, inc);
    insertion_sort_sentinel(start, end);
}

// Ciura
static void shell_sort_ciura(T * const start, T * const end) {
    size_t const gaps[] = { 301, 132, 57, 23, 10, 4 };
    for (size_t i = 0; i < 4; i++)
        for (size_t j = 0; j < gaps[i]; j++)
            insertion_sort_with_steps(&start[j], end, gaps[i]);
    for (size_t i = 4; i < 6; i++)
        for (size_t j = 0; j < gaps[i]; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, gaps[i]);
    insertion_sort_sentinel(start, end);
}

// Ciura-like
static void shell_sort_custom(T * const start, T * const end) {
    if (end - start + 1 <= 16) {
        insertion_sort_sentinel(start, end);
        return;
    }
    size_t const gaps[] = { 7 };
    for (size_t i = 0; i < sizeof gaps / sizeof gaps[0]; i++)
        for (size_t j = 0; j < gaps[i]; j++)
            insertion_sort_with_steps_sentinel(&start[j], end, gaps[i]);
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
 * @param end The (inclusive) end of said array.
 *
 * @return The pivot element.
**/
static inline T get_pivot(T const * const start, T const * const end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
    /* Always the leftmost element. */
    // return *start;
    /* The mean of the leftmost and the rightmost element. */
    return (*start + *end) / 2;
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
    if (end - start + 1 <= QUICK_TO_INSERTION) {  // false if `end < start` due to wrapping
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
float sqroot_on_dpu(float square) {
    float root = square / 3;
    if (square <= 0) return 0;
    for (uint32_t i = 0; i < 16; i++)
        root = (root + square / root) / 2;
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
perfcounter_t get_mean_of_time(perfcounter_t zeroth, perfcounter_t first) {
    return first / zeroth;
};

/**
 * @brief The standard deviation of measured times.
 * 
 * @param zeroth The zeroth moment, that is, the number of measurements.
 * @param first The first moment, that is, the sum of measured times.
 * @param second The second moment, that is, the sum of squared measured times.
 * 
 * @return The standard deviation.
**/
perfcounter_t get_std_of_time(perfcounter_t zeroth, perfcounter_t first, perfcounter_t second) {
    return sqroot_on_dpu((zeroth * second - first * first) / (zeroth * (first - 1)));
};

// the number of tested base sorting algorithms
#define NUM_OF_ALGOS (1)

void test_base_sorts(triple_buffers *buffers, struct dpu_arguments *args) {
    if (me() != 0) return;
    T *cache = buffers->cache;

    for (int i = 0; i < 16; i++)
        cache[i] = T_MIN;
    cache += 16;

    size_t const max_length = (TRIPLE_BUFFER_SIZE > args->length) ? args->length : TRIPLE_BUFFER_SIZE;
    perfcounter_config(COUNT_CYCLES, 1);
    perfcounter_t first_moments[NUM_OF_ALGOS], second_moments[NUM_OF_ALGOS], curr_time;
    struct { base_sort_algo *algo; char name[20]; } const algos[] = {
        // { insertion_sort_nosentinel, "Insert" },
        // { insertion_sort_sentinel, "Insert (Sent.)" },
        // { shell_sort_classic, "Shell" },
        // { shell_sort_ciura, "Shell (Ciura)" },
        // { shell_sort_custom, "Shell (Custom)" },
        { quick_sort_recursive, "Quick (Rec.)" },
        // { quick_sort_iterative, "Quick (It.)" }
    };
    assert(sizeof algos / sizeof algos[1] == NUM_OF_ALGOS);

    /* Do warm-up repetitions. */
    mram_range range = { 0, max_length };
    for (uint32_t rep = 0; rep < args->n_warmup; rep++) {
        generate_uniform_distribution(cache, max_length, args->upper_bound);
        insertion_sort_sentinel(&cache[0], &cache[max_length-1]);
    }

    /* Do and time actual repetitions. */
    printf("TEST: BASE SORTING ALGORITHMS (C: cycles, n: length)\n\n");
    size_t const lengths[] = { 8, 12, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048, 3072 };
    for (size_t li = 0; li < sizeof lengths / sizeof lengths[0]; li++) {
        size_t const length = lengths[li];
        if (length > TRIPLE_BUFFER_SIZE) break;
        range.end = length;
        memset(first_moments, 0, sizeof first_moments);
        memset(second_moments, 0, sizeof second_moments);
        /* Generate data, sort it, and take time. */
        for (uint32_t rep = 0; rep < args->n_reps; rep++) {
            for (size_t id = 0; id < NUM_OF_ALGOS; id++) {
                generate_uniform_distribution(cache, length, args->upper_bound);
                curr_time = perfcounter_get();
                algos[id].algo(&cache[0], &cache[length-1]);
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
        /* Print measurements. */
        size_t const log = 31 - __builtin_clz(length);
        printf("Length: %zd\n", length);
        for (size_t id = 0; id < NUM_OF_ALGOS; id++) {
            perfcounter_t mean = get_mean_of_time(args->n_reps, first_moments[id]);
            perfcounter_t std = get_std_of_time(args->n_reps, first_moments[id], second_moments[id]);
            printf(
                "%-14s: %8lu ±%3lu C \t %5lu C/n \t %5lu C/(n log n) \t %5lu C/n²\n",
                algos[id].name,
                mean,
                std,
                mean / length,
                mean / (length * log),
                mean / (length * length)
            );
        }
        printf("\n");
    }
}