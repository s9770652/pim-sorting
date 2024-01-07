#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#include <alloc.h>
#include <perfcounter.h>

#include "../support/common.h"
#include "checkers.h"

void get_time(perfcounter_t *cycles, char *label) {
    perfcounter_t cycles_total = 0, cycles_max = 0;
    for (unsigned t = 0; t < NR_TASKLETS; t++) {
        cycles_total += cycles[t];
        cycles_max = cycles_max < cycles[t] ? cycles[t] : cycles_max;
    }
    printf(
        "time (%s):\t%8.2f ms | %8.2f ms\n",
        label,
        (double)cycles_max / CLOCKS_PER_SEC * 1000,
        (double)cycles_total / CLOCKS_PER_SEC * 1000
    );
}

void print_array(T arr[], size_t len) {
    for (size_t i = 0; i < len; i++) {
#if UINT32
        printf("%d ", arr[i]);
#else
        printf("%lu ", arr[i]);
#endif
    }
    printf("\n");
}

bool is_sorted(T arr[], size_t len) {
    for (size_t i = 1; i < len; i++) {
        if (arr[i-1] > arr[i]) {
            return false;
        }
    }
    return true;
}

bool is_uniform(T arr[], size_t len, T upper_bound) {
    T* count = mem_alloc(upper_bound << DIV);
    for (T i = 0; i < upper_bound; i++) {
        count[i] = 0;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        count[arr[i]]++;
        sum += arr[i];
    }
    float mean = (double)sum / len;
    float variance = 0;
    for (size_t i = 0; i < upper_bound; i++) {
        variance += ((float)i - mean) * ((float)i - mean) * count[i];
    }
    variance /= (len - 1);
    // printf("is_uniform: ");
    // print_array(count, upper_bound);
    // printf("Mean: %f (%f)\n", mean, (upper_bound-1) / 2.);
    // printf("Variance: %f (%f)\n", variance, 1/12. * (upper_bound * upper_bound - 1));
    return (0.9 <= mean/((upper_bound-1) / 2.) <= 1.1 && 0.9 <= variance/(1/12. * (upper_bound * upper_bound - 1)) <= 1.1);
}