#ifndef _BASE_SORTS_H_
#define _BASE_SORTS_H_

#include "buffers.h"
#include "common.h"

/**
 * @brief Measures several sorting algorithms for a full WRAM cache.
 * 
 * @param buffers A struct containing a WRAM cache.
 * @param args The arguments with which the program was started.
**/
void test_wram_sorts(triple_buffers *buffers, struct dpu_arguments *args);

/**
 * @brief Measures the runtimes of ShellSorts with different step sizes on small arrays.
 * Before the final InsertionSort, there will be one pass with step size ∈ {2, …, 9}.
 * IF `BIG_STEP` is greater than 9, there will be another pass with step size `BIG_STEP`.
 * 
 * @param buffers A struct containing a WRAM cache.
 * @param args The arguments with which the program was started.
**/
void test_very_small_sorts(triple_buffers *buffers, struct dpu_arguments *args);

#endif // _BASE_SORTS_H_
