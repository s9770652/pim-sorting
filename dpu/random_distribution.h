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
 * @brief Uniformly draws numbers.
 * Stores them in a WRAM array.
 * 
 * @param start The first element of the array where to store the random data.
 * @param end The last element of said array.
 * @param upper_bound The (exclusive) upper limit of the range to draw from.
**/
void generate_uniform_distribution_wram(T *start, T *end, T upper_bound);

/**
 * @brief Uniformly draws numbers.
 * Stores them in an MRAM array.
 * 
 * @param array The MRAM array where to store the random data.
 * @param cache A cache in WRAM.
 * @param range For which indices of the array the numbers are drawn.
 * @param upper_bound The (exclusive) upper limit of the range to draw from.
**/
void generate_uniform_distribution_mram(T __mram_ptr *array, T *cache, mram_range const *range,
        T upper_bound);

/**
 * @brief Generates a range of ascending numbers, starting from `T_MIN`.
 * Stores them in a WRAM array.
 * 
 * @param start The first element of the array where to store the random data.
 * @param end The last element of said array.
**/
void generate_sorted_distribution_wram(T *start, T *end);

/**
 * @brief Generates a range of descending numbers, ending at `T_MIN`.
 * Stores them in a WRAM array.
 * 
 * @param start The first element of the array where to store the random data.
 * @param end The last element of said array.
**/
void generate_reverse_sorted_distribution_wram(T *start, T *end);

/**
 * @brief Generates a range of ascending numbers, then swaps some neighbours.
 * Stores them in a WRAM array.
 * 
 * @param start The first element of the array where to store the random data.
 * @param end The last element of said array.
 * @param swaps The number of swaps of neighboured numbers. If zero, √n swaps are made.
**/
void generate_almost_sorted_distribution_wram(T *start, T *end, size_t swaps);

#endif  // _RANDOM_DISTRIBUTION_H_
