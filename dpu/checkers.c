#include <stdio.h>
#include <stdint.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <string.h>

#include "checkers.h"

// The counts of the values in the range `[0, NR_COUNTS - 1]` are stored by `array_stats`.
#define NR_COUNTS (sizeof(((array_stats *)0)->counts) / sizeof(((array_stats *)0)->counts[0]))

uint64_t sums[NR_TASKLETS];
size_t counts[NR_TASKLETS][NR_COUNTS];
bool unsorted[NR_TASKLETS];

MUTEX_INIT(printing_mutex);
BARRIER_INIT(checking_barrier, NR_TASKLETS);

void print_array(T __mram_ptr *array, T *cache, size_t const length, char *label) {
    if (me() != 0) return;
    if (length > 2048) return;
    printf("%s:\n", label);
    size_t i, curr_length, curr_size;
    mram_range range = { 0, length };
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        mram_read(&array[i], cache, curr_size);
        print_single_line(cache, curr_length);
    }
    printf("\n");
}

void print_single_line(T *cache, size_t length) {
    char colours[8][9] = {
        "\x1b[0;100m", "\x1b[0;101m", "\x1b[0;102m", "\x1b[0;103m",
        "\x1b[0;104m", "\x1b[0;105m", "\x1b[0;106m", "\x1b[0;107m",
    };
    char *colour;
    mutex_lock(printing_mutex);
    for (size_t i = 0; i < length; i++) {
        colour = (cache[i] < 8) ? colours[cache[i]] : ANSI_COLOR_RESET;
        printf("%s%3"T_QUALIFIER" ", colour, cache[i]);
    }
    printf(ANSI_COLOR_RESET "\n");
    mutex_unlock(printing_mutex);
}

#if (CHECK_SANITY)

/**
 * @brief Reduces `sums`, `counts`, and `unsorted`.
 * 
 * @param dummy Whether a dummy value was set.
 * @param result The struct where the results are stored.
**/
static void accumulate_stats(bool dummy, array_stats *result) {
    if (me() != 0) return;
    // Gather statistics.
    for (size_t t = 1; t < NR_TASKLETS; t++) {
        sums[0] += sums[t];
        for (size_t j = 0; j < NR_COUNTS; j++) {
            counts[0][j] += counts[t][j];
        }
        unsorted[0] |= unsorted[t];
    }
    // Write statistics onto the appropriate memory address.
    result->sum = sums[0];
    result->sum -= (dummy) ? UINT32_MAX : 0;  // The dummy value is at the end of the last range.
    memcpy(&result->counts, counts[0], NR_COUNTS * sizeof(counts[0][0]));
    result->unsorted = unsorted[0];
}

void get_stats_unsorted(T __mram_ptr const * const array, T * const cache, mram_range const range,
        bool const dummy, array_stats * const result) {
    // Reset values.
    sums[me()] = 0;
    for (size_t i = 0; i < NR_COUNTS; i++) {
        counts[me()][i] = 0;
    }
    // Calculate statistics.
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        mram_read(&array[i], cache, curr_size);
        for (size_t j = 0; j < curr_length; j++) {
            sums[me()] += cache[j];
            if (cache[j] < NR_COUNTS) {
                counts[me()][cache[j]]++;
            }
        }
    }
    barrier_wait(&checking_barrier);
    accumulate_stats(dummy, result);
}

void get_stats_sorted(T __mram_ptr const * const array, T * const cache, mram_range const range,
        bool const dummy, array_stats * const result) {
    // Reset values.
    sums[me()] = 0;
    for (size_t i = 0; i < NR_COUNTS; i++) {
        counts[me()][i] = 0;
    }
    unsorted[me()] = false;
    T prev = (me() == 0) ? T_MIN : array[range.start-1];
    // Calculate statistics and check order.
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        mram_read(&array[i], cache, curr_size);
        unsorted[me()] |= prev > cache[0];
        for (size_t j = 0; j < curr_length; j++) {
            sums[me()] += cache[j];
            if (cache[j] < NR_COUNTS) {
                counts[me()][cache[j]]++;
            }
            unsorted[me()] |= cache[j-1] > cache[j];  // `j-1` possible due to the sentinel value
        }
        prev = cache[MAX_TRANSFER_LENGTH_TRIPLE-1];
    }
    barrier_wait(&checking_barrier);
    accumulate_stats(dummy, result);
}

void get_stats_unsorted_wram(T const array[], size_t const length, array_stats *result) {
    // Reset values.
    sums[me()] = 0;
    for (size_t i = 0; i < NR_COUNTS; i++) {
        counts[me()][i] = 0;
    }
    // Calculate statistics.
    for (size_t j = 0; j < length; j++) {
        sums[me()] += array[j];
        if (array[j] < NR_COUNTS) {
            counts[me()][array[j]]++;
        }
    }
    accumulate_stats(false, result);
}

void get_stats_sorted_wram(T const array[], size_t const length, array_stats *result) {
    // Reset values.
    sums[me()] = 0;
    for (size_t i = 0; i < NR_COUNTS; i++) {
        counts[me()][i] = 0;
    }
    unsorted[me()] = false;
    // Calculate statistics and check order.
    for (size_t j = 0; j < length; j++) {
        sums[me()] += array[j];
        if (array[j] < NR_COUNTS) {
            counts[me()][array[j]]++;
        }
        unsorted[me()] |= array[j-1] > array[j];  // `j-1` possible due to the sentinel value
    }
    accumulate_stats(false, result);
}

bool compare_stats(array_stats const * const stats_1, array_stats const * const stats_2,
        bool const print_on_success) {
    if (me() != 0) return EXIT_SUCCESS;
    bool same_elements = stats_1->sum == stats_2->sum;
    same_elements &= memcmp(stats_1->counts, stats_2->counts, NR_COUNTS*sizeof(counts[0][0])) == 0;
    if (!same_elements) {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements have changed.\n");
        printf("\nSums: %lu ↔ %lu\nCounts: ", stats_1->sum, stats_2->sum);
        for (size_t c = 0; c < NR_COUNTS; c++) {
            printf("%d: %zu ↔ %zu   ", c, stats_1->counts[c], stats_2->counts[c]);
        }
        printf("\n");
    }
    if (stats_2->unsorted) {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements are not sorted.\n");
    }
    if (!same_elements || stats_2->unsorted)
        return EXIT_FAILURE;
    if (print_on_success)
        printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Elements are correctly sorted.\n");
    return EXIT_SUCCESS;
}

#endif  // CHECK_SANITY
