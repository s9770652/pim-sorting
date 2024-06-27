#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "random_distribution.h"

#include <string.h>

/**
 * @brief Uniformly draw a random number using rejection sampling.
 * 
 * @param s The upper limit (inclusive) of the range to draw from.
 * 
 * @return A uniformly drawn integer between `0` and `s`.
**/
static inline int round_reject(int s) {
    size_t const mask = (1 << ((sizeof(int) * CHAR_BIT) - __builtin_clz(s))) - 1;
    int random;
    do {
        random = rand() & mask;
    } while (random > s);
    return random;
}

/**
 * @brief Generates a range of ascending numbers, starting from `T_MIN`.
 * 
 * @param array Where the numbers are to be stored.
 * @param length How many numbers are to be generated.
 * @param smallest_value The value of the first element.
**/
void generate_sorted_distribution(T array[], size_t const length, T const smallest_value) {
    for (size_t i = 0; i < length; i++) {
        array[i] = i + smallest_value;
    }
}

/**
 * @brief Generates a range of descending numbers, ending at `T_MIN`.
 * 
 * @param array Where the numbers are to be stored.
 * @param length How many numbers are to be generated.
 * @param smallest_value The value of the last element.
**/
void generate_reversed_sorted_distribution(T array[], size_t const length, T const smallest_value) {
    T const base_value = smallest_value + length - 1;
    for (size_t i = 0; i < length; i++) {
        array[i] = base_value - i;
    }
}

/**
 * @brief Generates a range of ascending numbers, then swaps some pairs of numbers.
 * Does not check whether some pairs have common elements.
 * 
 * @param array Where the numbers are to be stored.
 * @param length How many numbers are to be generated.
 * @param swaps How many pairs are to be swapped. If zero, √n swaps are made.
**/
void generate_almost_sorted_distribution(T array[], size_t const length, size_t swaps) {
    generate_sorted_distribution(array, length, 0);
    swaps = (swaps) ? : sqrt(length);
    for (size_t s = 0; s < swaps; s++) {
        size_t const i = round_reject(length - 1);
        size_t j;
        do { j = round_reject(length - 1); } while (i == j);
        swap(&array[i], &array[j]);
    }
}

/**
 * @brief Uniformly and independently draws numbers.
 * 
 * @param array Where the numbers are to be stored.
 * @param length How many numbers are to be generated.
 * @param upper_bound The (exclusive) upper limit of the range to draw from.
**/
void generate_uniform_distribution(T array[], size_t const length, T const upper_bound) {
    bool const is_power_of_two = (upper_bound & (upper_bound - 1)) == 0;
    if (upper_bound == 0) {
        for (size_t i = 0; i < length; i++)
            array[i] = rand();
    } else if (is_power_of_two) {
        for (size_t i = 0; i < length; i++)
            array[i] = rand() & (upper_bound - 1);
    } else {
        for (size_t i = 0; i < length; i++)
            array[i] = round_reject(upper_bound - 1);
    }
}

/**
 * @brief Draws from 100 possible numbers according to a powerlaw with the parameters 𝘢 = 1 and
 * 𝘣 = –0.75. In other words, the element 𝘬 ∈ [1, 100] is drawn with a probability proportional
 * to 1/𝘬⁰ᐧ⁷⁵.
 * 
 * @param array Where the numbers are to be stored.
 * @param length How many numbers are to be generated.
**/
void generate_zipf_distribution(T array[], size_t const length) {
    /* Calculate CDF once. */
    static double cdf[100 + 1], total_density = 0;
    size_t const max = sizeof cdf / sizeof cdf[0] - 1;
    static bool set_up = false;
    if (!set_up) {
        cdf[0] = 0;
        for (size_t k = 1; k <= max; k++) {  // Calculating CDF.
            cdf[k] = cdf[k - 1] + 1/pow(k, 0.75);
            total_density += (cdf[k] - cdf[k - 1]);
        }
        for (size_t k = 1; k <= max; k++) {  // Normalising CDF.
            cdf[k] /= total_density;
        }
        cdf[max] = 1;
        set_up = true;
    }
    /* Draw numbers. */
    for (size_t i = 0; i < length; i++) {
        double r = (double)rand() / RAND_MAX;
        for (size_t k = 1; k <= max; k++) {  // A linear search as a binary search is not worth it.
            if (r <= cdf[k]) {
                array[i] = k;
                break;
            }
        }
    }
}

void generate_input_distribution(T array[], size_t const length, enum dist const type,
        T const param) {
    switch (type) {
    case sorted: generate_sorted_distribution(array, length, param); break;
    case reverse: generate_reversed_sorted_distribution(array, length, param); break;
    case almost: generate_almost_sorted_distribution(array, length, param); break;
    case uniform: generate_uniform_distribution(array, length, param); break;
    case zipf: generate_zipf_distribution(array, length); break;
    default: break;
    }
}
