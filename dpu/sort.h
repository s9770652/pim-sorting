#ifndef _SORT_H_
#define _SORT_H_

#include <stdbool.h>
#include <stddef.h>

#include "buffers.h"
#include "common.h"

bool sort(T __mram_ptr *input, T __mram_ptr *output, triple_buffers *buffers, const mram_range ranges[NR_TASKLETS]);

#endif  // _SORT_H_