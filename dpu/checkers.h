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

/**
 * @brief Start (inclusive) and end (exclusive) of the range
 * traversed by a tasklet in `LOOP_ON_MRAM`.
**/
typedef struct mram_range {
    /// @brief The index at which to start.
    uint32_t start;
    /// @brief The first index to not include.
    uint32_t end;
    /// @brief The first index to not include. Must be chosen such that the size is aligned on 8 bytes.
    uint32_t end_aligned;
} mram_range;

/**
 * @brief Blockwise iterator over the MRAM indices in the range as defined by `range`.
 * 
 * @param i Current index, i. e. the start of the current block.
 * @param curr_length Length of the current block as number of elements.
 * @param curr_size Size of the current block, aligned on 8 bytes.
 * @param range Start and end of the tasklet's range.
**/
#define LOOP_ON_MRAM(i, curr_length, curr_size, range)                                             \
for (                                                                                              \
    i = range.start,                                                                               \
    curr_length = (i + BLOCK_LENGTH > range.end) ? range.end - i : BLOCK_LENGTH,                   \
    curr_size = (i + BLOCK_LENGTH > range.end) ? (range.end_aligned - i) << DIV : BLOCK_SIZE;      \
    i < range.end;                                                                                 \
    i += BLOCK_LENGTH,                                                                             \
    curr_length = (i + BLOCK_LENGTH > range.end) ? range.end - i : BLOCK_LENGTH,                   \
    curr_size = (i + BLOCK_LENGTH > range.end) ? (range.end_aligned - i) << DIV : BLOCK_SIZE       \
)

/**
 * @fn get_time
 * @brief Prints the maximum duration and total time of an array of cycle counts.
 * 
 * `perfcounter_config` must have been called with `COUNT_CYCLES` beforehand.
 * The current cycle counts in `cycles` should be gotten using `perfcounter_get`.
 * 
 * @param cycles The array of cycle counts to use for calculation.
 * @param label The label to print in front of the duration and total time.
**/
void get_time(perfcounter_t *cycles, char *label);

/**
 * @fn print_array
 * @brief Prints an array of integers and separates the numbers with blanks.
 * 
 * @param arr The array of integers to print.
 * @param len Up to which element to print (exclusive).
**/
void print_array(T arr[], size_t len);

/**
 * @fn is_sorted
 * @brief Checks whether an array is sorted. Currently sequentially implemented.
 * 
 * @param arr The array of integers to check.
 * @param len Up to which element to check the order (exclusive).
 * 
 * @returns `1` if the array is sorted, elsewise `0`.
**/
bool is_sorted(T arr[], size_t len);

/**
 * @fn is_uniform
 * @brief Checks the mean and variance of an array of integers
 * and compares them with an ideal uniform distribution.
 * Currently sequentially implemented.
 * 
 * @param arr The array of integers to check.
 * @param len Up to which element to check the mean and variance.
 * 
 * @returns `1` if the mean and variance deviate at most 10 % from the ideal, elsewise `0`.
**/
bool is_uniform(T arr[], size_t len, T upper_bound);

#endif  // _CHECKERS_H_