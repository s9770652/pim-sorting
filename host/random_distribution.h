/**
 * @file
 * @brief Populating arrays with randomly drawn numbers through a CPU.
**/

#ifndef _RANDOM_DISTRIBUTION_H_
#define _RANDOM_DISTRIBUTION_H_

#include <stddef.h>

#include "common.h"

enum dist { sorted, reverse, almost, zeroone, uniform, zipf, normal, nr_of_dists };

/**
 * @brief Generates a sequence of numbers according to some random distribution.
 * 
 * @param start The first element of the array where to store the random data.
 * @param end The last element of said array.
 * @param type The distribution type to draw from.
 * @param param What parameter is used for the distribution.
**/
void generate_input_distribution(T array[], size_t const length, enum dist type, T param);

/**
 * @brief Returns the name of a given distribution.
 * 
 * @param type The distribution type whose name to return.
 * 
 * @return The distribution name.
**/
static inline char *get_dist_name(enum dist type) {
    switch (type) {
    case sorted: return "sorted";
    case reverse: return "reverse";
    case almost: return "almost";
    case zeroone: return "zero-one";
    case uniform: return "uniform";
    case zipf: return "Zipf";
    case normal: return "normal";
    default: return "";
    }
}

#endif  // _RANDOM_DISTRIBUTION_H_
