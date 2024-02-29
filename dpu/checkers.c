#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <string.h>

#include "../support/common.h"
#include "checkers.h"
#include "mram_loop.h"

uint64_t sums[NR_TASKLETS];
size_t counts[NR_TASKLETS][NR_COUNTS];
bool sorted;

MUTEX_INIT(printing_mutex);
MUTEX_INIT(checking_mutex);
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
    char colours[8][9] = { "\x1b[0;100m", "\x1b[0;101m", "\x1b[0;102m", "\x1b[0;103m", "\x1b[0;104m", "\x1b[0;105m", "\x1b[0;106m", "\x1b[0;107m" };
    char *colour;
    mutex_lock(printing_mutex);
    for (size_t i = 0; i < length; i++) {
        colour = (cache[i] < 8) ? colours[cache[i]] : ANSI_COLOR_RESET;
#if UINT32
        printf("%s%3d ", colour, cache[i]);
#else
        printf("%s%3lu ", colour, arr[i]);
#endif
    }
    printf(ANSI_COLOR_RESET "\n");
    mutex_unlock(printing_mutex);
}

void get_sum(T __mram_ptr *array, T *cache, size_t const length, array_stats *result) {
    sums[me()] = 0;
    for (size_t i = 0; i < NR_COUNTS; i++) {
        counts[me()][i] = 0;
    }

    size_t i, curr_length, curr_size;
    mram_range range = { 0, length };
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        mram_read(&array[i], cache, curr_size);
        for (size_t x = 0; x < curr_length; x++) {
            sums[me()] += cache[x];
            if (cache[x] < 8) {
                counts[me()][cache[x]]++;
            }
        }
    }
    barrier_wait(&checking_barrier);

    if (me() != 0) return;
    for (size_t t = 1; t < NR_TASKLETS; t++) {
        sums[me()] += sums[t];
        for (size_t i = 1; i < NR_COUNTS; i++) {
            counts[me()][i] += counts[t][i];
        }
    }
    result->sum = sums[me()];
    memcpy(&result->counts, counts[me()], NR_COUNTS * sizeof(size_t));
}

bool compare_stats(array_stats *stats_1, array_stats *stats_2) {
    if (me() != 0) return false;
    bool same_elems = stats_1->sum == stats_2->sum;
    for (size_t c = 0; c < NR_COUNTS; c++) {
        same_elems = same_elems && stats_1->counts[c] == stats_2->counts[c];
    }
    if (!same_elems) {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements have changed.\n");
        printf("\nSums: %lu ↔ %lu\nCounts: ", stats_1->sum, stats_2->sum);
        for (size_t c = 0; c < NR_COUNTS; c++) {
            printf("%d: %zu ↔ %zu   ", c, stats_1->counts[c], stats_2->counts[c]);
        }
        printf("\n");
    }
    return same_elems;
}

bool is_sorted(T __mram_ptr *array, T *cache, mram_range range) {
    if (me() == 0) sorted = true;
    barrier_wait(&checking_barrier);
    T prev = (me() == 0) ? 0 : array[range.start-1];
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, range) {
        if (!sorted) break;
        mram_read(&array[i], cache, curr_size);
        if ((prev > cache[0]) || (!is_single_line_sorted(cache, curr_length))) {
            mutex_lock(checking_mutex);
            sorted = false;
            mutex_unlock(checking_mutex);
            break;
        }
        prev = cache[BLOCK_LENGTH-1];
    }
    barrier_wait(&checking_barrier);
    if (me() == 0 && !sorted) {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements are not sorted.\n");
    }
    return sorted;
}

bool is_single_line_sorted(T *cache, size_t length) {
    for (size_t i = 1; i < length; i++) {
        if (cache[i-1] > cache[i]) {
            return false;
        }
    }
    return true;
}

bool is_uniform(T *array, size_t length, T upper_bound) {
    T* count = mem_alloc(upper_bound << DIV);
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