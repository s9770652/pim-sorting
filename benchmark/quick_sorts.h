#ifndef _QUICK_SORTS_H_
#define _QUICK_SORTS_H_

#include "buffers.h"
#include "common.h"

/**
 * @brief Measures several implementations of QuickSort.
 * 
 * @param buffers A struct containing a WRAM cache.
 * @param args The arguments with which the program was started.
**/
void test_quick_sorts(triple_buffers *buffers, struct dpu_arguments *args);

#endif // _QUICK_SORTS_H_
