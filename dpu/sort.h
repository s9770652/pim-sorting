#ifndef _SORT_H_
#define _SORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <seqread.h>

#include "../support/common.h"

void insertion_sort(T arr[], size_t len);

void deplete_reader(T __mram_ptr *output, T *cache, size_t i, T *ptr, seqreader_t *sr, const T __mram_ptr *end, size_t written);

bool merge(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range range);

bool sort(T __mram_ptr *input, T __mram_ptr *output, T *cache, const mram_range range);

#endif  // _SORT_H_