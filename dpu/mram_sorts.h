#ifndef _MRAM_SORTS_H_
#define _MRAM_SORTS_H_

#include <stdbool.h>

#include <mram.h>

#include "common.h"

extern bool flipped[NR_TASKLETS];  // Whether a write-back from the auxiliary array is (not) needed.

void merge_sort_mram(T __mram_ptr * const start, T __mram_ptr * const end);

#endif  // _MRAM_SORTS_H_
