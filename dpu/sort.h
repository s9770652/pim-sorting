#ifndef _SORT_H_
#define _SORT_H_

#include <stddef.h>

#include "../support/common.h"

void insertion_sort(T arr[], size_t len);

void sort(T __mram_ptr *elements, T *cache);

#endif  // _SORT_H_