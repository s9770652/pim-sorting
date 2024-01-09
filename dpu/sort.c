#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <alloc.h>
#include <defs.h>
#include <mram.h>

#include "../support/common.h"
#include "checkers.h"
#include "sort.h"

#define INSERTION_SIZE 2048

void insertion_sort(T arr[], size_t len) {
    for (size_t i = 1; i < len; i++) {
        T to_sort = arr[i];
        size_t j = i;
        while ((j > 0) && (arr[j-1] > to_sort)) {
            arr[j] = arr[j-1];
            j--;
        }
        arr[j] = to_sort;
    }
}

void sort(T __mram_ptr *elements, T *cache) {
    /* Insertion sort by each tasklet. */

    /* Merge by tasklets. */
}