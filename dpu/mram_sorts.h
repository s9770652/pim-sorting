/**
 * @file
 * @brief Sequential sorting of MRAM data through the fastest full-space MergeSort.
**/
#ifndef _MRAM_SORTS_H_
#define _MRAM_SORTS_H_

#include <stdbool.h>

#include <mram.h>

#include "common.h"

extern bool flipped[NR_TASKLETS];  // Whether `output` contains the latest sorted runs.

/**
 * @brief A sequential MRAM implementation of full-space MergeSort.
 * 
 * @param start The first item of the MRAM array to sort.
 * @param end The last item of said array.
**/
void merge_sort_mram(T __mram_ptr * const start, T __mram_ptr * const end);

#endif  // _MRAM_SORTS_H_
