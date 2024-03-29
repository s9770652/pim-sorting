/**
 * @file
 * @brief Populating arrays with randomly drawn numbers.
**/

#ifndef _RANDOM_DISTRIBUTION_H_
#define _RANDOM_DISTRIBUTION_H_

#include "common.h"
#include "mram_loop.h"
#include "random_generator.h"

/// @brief Contains the seed for each tasklet.
extern struct xorshift rngs[NR_TASKLETS];

/**
 * @brief Uniformly draws numbers and stores them in a WRAM cache.
 * 
 * @param cache A cache in WRAM where the random numbers are stored.
 * @param length The number of numbers to draw.
 * @param upper_bound The upper limit (exclusive) of the range to draw from.
**/
void generate_uniform_distribution(T * cache, size_t length, T upper_bound);

/**
 * @brief Uniformly draws numbers and stores them in an MRAM array.
 * 
 * @param array The MRAM array where to store the random data.
 * @param cache A cache in WRAM.
 * @param range For which indices of the array the numbers are drawn.
 * @param upper_bound The upper limit (exclusive) of the range to draw from.
**/
void generate_uniform_distribution_mram(T __mram_ptr *array, T * cache, mram_range const * range,
        T upper_bound);

#endif  // _RANDOM_DISTRIBUTION_H_