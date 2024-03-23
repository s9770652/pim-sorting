/**
 * @file
 * @brief Populating an MRAM array with randomly drawn numbers.
**/

#ifndef _RANDOM_DISTRIBUTION_H_
#define _RANDOM_DISTRIBUTION_H_

#include "common.h"
#include "mram_loop.h"
#include "random_generator.h"

extern struct xorshift rngs[NR_TASKLETS];

/**
 * @brief Uniformly draws numbers and stores them in an MRAM array.
 * 
 * @param array The MRAM array where to store the random data.
 * @param cache A cache in WRAM.
 * @param range For which indices of the array the numbers are drawn.
 * @param upper_bound The upper limit (exclusive) of the range to draw from.
 */
void generate_uniform_distribution(T __mram_ptr *array, T * const cache,
        mram_range const * const range, T const upper_bound);

#endif  // _RANDOM_DISTRIBUTION_H_