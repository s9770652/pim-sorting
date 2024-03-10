#ifndef _SORT_H_
#define _SORT_H_

#include <stdbool.h>
#include <stddef.h>

#include "../support/common.h"
#include "buffers.h"

bool merge(T __mram_ptr *input, T __mram_ptr *output, wram_buffers *buffers, const mram_range ranges[NR_TASKLETS]);

bool sort(T __mram_ptr *input, T __mram_ptr *output, wram_buffers *buffers, const mram_range ranges[NR_TASKLETS]);

#endif  // _SORT_H_