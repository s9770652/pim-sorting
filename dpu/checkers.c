#include <stdio.h>
#include <stdint.h>

#include <alloc.h>

#include "../support/common.h"
#include "checkers.h"

void print_array(T arr[], uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        #if UINT32
        printf("%d ", arr[i]);
        #else
        printf("%lu ", arr[i]);
        #endif
    }
    printf("\n");
}

int is_sorted(T arr[], uint32_t len) {
    for (uint32_t i = 1; i < len; i++) {
        if (arr[i-1] > arr[i]) {
            return 0;
        }
    }
    return 1;
}

int is_uniform(T arr[], uint32_t len, T upper_bound) {
    T* count = mem_alloc(upper_bound << DIV);
    for (T i = 0; i < upper_bound; i++) {
        count[i] = 0;
    }
    int64_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        count[arr[i]]++;
        sum += arr[i];
    }
    float mean = (double)sum / len;
    float variance = 0;
    for (uint32_t i = 0; i < upper_bound; i++) {
        variance += ((float)i - mean) * ((float)i - mean) * count[i];
    }
    variance /= (len - 1);
    // printf("is_uniform: ");
    // print_array(count, upper_bound);
    // printf("Mean: %f (%f)\n", mean, (upper_bound-1) / 2.);
    // printf("Variance: %f (%f)\n", variance, 1/12. * (upper_bound * upper_bound - 1));
    return (0.9 <= mean/((upper_bound-1) / 2.) <= 1.1 && 0.9 <= variance/(1/12. * (upper_bound * upper_bound - 1)) <= 1.1);
}