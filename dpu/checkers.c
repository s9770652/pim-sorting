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
bool sorted[NR_TASKLETS];

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
        "\x1b[0;104m", "\x1b[0;105m", "\x1b[0;106m", "\x1b[0;107m"
    };
    char *colour;
    mutex_lock(printing_mutex);
    for (size_t i = 0; i < length; i++) {
        colour = (cache[i] < 8) ? colours[cache[i]] : ANSI_COLOR_RESET;
#if UINT32
        printf("%s%3u ", colour, cache[i]);
#else
        printf("%s%3lu ", colour, cache[i]);
#endif
    }
    printf(ANSI_COLOR_RESET "\n");
    mutex_unlock(printing_mutex);
}

/**
 * @brief Reduces `sums`, `counts`, and `sorted`.
 * 
 * @param dummy Whether a dummy value was set.
 * @param result The struct where the results are stored.
**/
static void accumulate_stats(bool dummy, array_stats *result) {
    if (me() != 0) return;
    // Gather statistics.
    for (size_t t = 1; t < NR_TASKLETS; t++) {
        sums[me()] += sums[t];
        for (size_t j = 0; j < NR_COUNTS; j++) {
            counts[me()][j] += counts[t][j];
        }
        sorted[me()] &= sorted[t];
    }
    // Write statistics onto the appropriate memory address.
    result->sum = sums[me()];
    result->sum -= (dummy) ? UINT32_MAX : 0;  // The dummy value is at the end of the last range.
    memcpy(&result->counts, counts[me()], NR_COUNTS * sizeof(size_t));
    result->sorted = sorted[me()];
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
            if (cache[j] < 8) {
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
    sorted[me()] = true;
    T prev = (me() == 0) ? T_MIN : array[range.start-1];
    // Calculate statistics and check order.
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        mram_read(&array[i], cache, curr_size);
        sorted[me()] &= prev <= cache[0];
        for (size_t j = 0; j < curr_length; j++) {
            sums[me()] += cache[j];
            if (cache[j] < 8) {
                counts[me()][cache[j]]++;
            }
            sorted[me()] &= cache[j-1] <= cache[j];  // `j-1` possible due to the sentinel value
        }
        prev = cache[BLOCK_LENGTH-1];
    }
    barrier_wait(&checking_barrier);
    accumulate_stats(dummy, result);
}

void compare_stats(array_stats const * const stats_1, array_stats const * const stats_2) {
    if (me() != 0) return;
    bool same_elements = stats_1->sum == stats_2->sum;
    same_elements &= memcmp(stats_1->counts, stats_2->counts, NR_COUNTS * sizeof(size_t)) == 0;
    if (!same_elements) {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements have changed.\n");
        printf("\nSums: %lu ↔ %lu\nCounts: ", stats_1->sum, stats_2->sum);
        for (size_t c = 0; c < NR_COUNTS; c++) {
            printf("%d: %zu ↔ %zu   ", c, stats_1->counts[c], stats_2->counts[c]);
        }
        printf("\n");
    }
    if (!stats_2->sorted) {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements are not sorted.\n");
    }
    if (same_elements && stats_2->sorted) {
        printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Elements are correctly sorted.\n");
    }
}

bool is_uniform(T *array, size_t length, T upper_bound) {
    T *count = mem_alloc(upper_bound * sizeof(T *));
    for (T i = 0; i < upper_bound; i++) {
        count[i] = 0;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        count[array[i]]++;
        sum += array[i];
    }
    float mean = (double)sum / length;
    float variance = 0;
    for (size_t i = 0; i < upper_bound; i++) {
        variance += ((float)i - mean) * ((float)i - mean) * count[i];
    }
    variance /= (length - 1);
    // printf("is_uniform: ");
    // print_array(count, upper_bound);
    // printf("Mean: %f (%f)\n", mean, (upper_bound-1) / 2.);
    // printf("Variance: %f (%f)\n", variance, 1/12. * (upper_bound * upper_bound - 1));
    return (0.9 <= mean/((upper_bound-1) / 2.) <= 1.1 && 0.9 <= variance/(1/12. * (upper_bound * upper_bound - 1)) <= 1.1);
}