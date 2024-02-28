#ifndef _SORT_H_
#define _SORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <seqread.h>

#include "../support/common.h"

void insertion_sort(T arr[], size_t len);

bool merge(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range ranges[NR_TASKLETS]);

bool sort(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range ranges[NR_TASKLETS]);

#endif  // _SORT_H_