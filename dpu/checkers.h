#ifndef _CHECKERS_H_
#define _CHECKERS_H_

/**
 * @file checkers.h
 * @brief Checking the sanity of generated and sorted numbers.
 * 
 * The use of some of these functions is intented for development purposes only.
**/

#include <stddef.h>

#include <perfcounter.h>

#include "../support/common.h"
#include "mram_loop.h"

/**
 * @brief Prints the maximum duration and total time of an array of cycle counts.
 * 
 * `perfcounter_config` must have been called with `COUNT_CYCLES` beforehand.
 * The current cycle counts in `cycles` should be gotten using `perfcounter_get`.
 * 
 * @param cycles The array of cycle counts to use for calculation.
 * @param label The label to print in front of the duration and total time.
**/
void print_time(perfcounter_t *cycles, char *label);

/**
 * @brief Prints an MRAM array in lines of `BLOCK_LENGTH` numbers.
 * The background colour is changed according to the values.
 * Only prints up to some maximum length (currently: 2048).
 * 
 * @param array The array to print.
 * @param cache The WRAM cache into which to load the array.
 * @param length The length of the MRAM array
 * @param label The string to put in the line above the array.
**/
void print_array(T __mram_ptr *array, T *cache, size_t const length, char *label);

/**
 * @brief Prints a WRAM array of integers in a single line.
 * The background colour is changed according to the values.
 * 
 * @param array The array of integers to print.
 * @param length Up to which element to print (exclusive).
**/
void print_single_line(T *array, size_t length);

#define NR_COUNTS 8
/**
 * @brief The sum of all elements in some array and the counts of the first `NR_COUNTS` elements.
 */
typedef struct array_stats
{
    /// @brief The sum of all elements in some array.
    uint64_t sum;
    /// @brief The counts of the first `NR_COUNTS` elements.
    size_t counts[NR_COUNTS];
} array_stats;

/**
 * @brief Calculates the sum of a given MRAM array
 * and also prints its content if the length is at most 2048.
 * For this reason, this function is currently sequential.
 * 
 * @param array The MRAM array whose sum is to be calculated.
 * @param cache A cache in WRAM.
 * @param counts Array of occurrences of each array element smaller than 8.
 * Should be initialised to all zeroes.
 * @param length The length of the MRAM array.
 * @param label The text to be shown before the array is printed.
 * 
 * @return The sum of all elements in `array`.
**/
void get_sum(T __mram_ptr *array, T *cache, size_t const length, array_stats *result);

/**
 * @brief Checks whether the sums of two arrays and their counts of each possible value are equal.
 * 
 * @param stats_1 The statistics of the first array.
 * @param stats_2 The statistics of the second array.
 * 
 * @return `true` if everything is equal, elsewise `false`.
**/
bool compare_stats(array_stats *stats_1, array_stats *stats_2);

/**
 * @brief Checks whether a given MRAM array is sorted.
 * 
 * @param array The MRAM array to be checked.
 * @param cache A cache in WRAM.
 * @param range The range of indices for the calling tasklet to check.
 * 
 * @return `true` if everything is sorted, elsewise `false`.
**/
bool is_sorted(T __mram_ptr *array, T *cache, mram_range range);

/**
 * @brief Checks whether an array is sorted.
 * 
 * @param array The array of integers to check.
 * @param length Up to which element to check the order (exclusive).
 * 
 * @returns `true` if the array is sorted, elsewise `false`.
**/
bool is_single_line_sorted(T *array, size_t length);

/**
 * @brief Checks the mean and variance of an array of integers
 * and compares them with an ideal uniform distribution.
 * @note Currently sequentially implemented.
 * 
 * @param array The array of integers to check.
 * @param length Up to which element to check the mean and variance.
 * 
 * @returns `true` if the mean and variance deviate at most 10 % from the ideal, elsewise `false`.
**/
bool is_uniform(T *array, size_t length, T upper_bound);

#endif  // _CHECKERS_H_