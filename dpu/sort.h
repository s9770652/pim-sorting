#ifndef _SORT_H_
#define _SORT_H_

#include <stddef.h>

#include "../support/common.h"

void insertion_sort(T arr[], size_t len);

void sort(T __mram_ptr *elements, T *cache, const size_t offset, const size_t length, const size_t length_aligned, const size_t block_length);

#endif  // _SORT_H_