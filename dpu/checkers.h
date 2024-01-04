#ifndef _CHECKERS_H_
#define _CHECKERS_H_

/**
 * @file checkers.h
 * @brief Checking the sanity of generated and sorted numbers.
 * 
 * The use of these functions is only intented for development purposes.
**/

#include "../support/common.h"

/**
 * @def LOOP_ON_MRAM
 * @brief Loop header for looping over an MRAM array to write to / read from.
 * It calucates the byte offsets (`bi`) and length of blocks (`lsb`) for all involved tasklets.
 * 
 * @param bi Current byte offset. Will be set automatically. Initially `1 * bt`.
 * @param bt Offset of first block of the respective tasklet. Must be a multiple of `bs`.
 * @param s Size of the MRAM array. Must be aligned on 8 bytes.
 * @param bs Maximum size of the blocks read at once.
 * @param nr Number of tasklets.
 * @param lsb Size of current block. Never greater than `bs`.
**/
#define LOOP_ON_MRAM(bi, bt, s, bs, nr, lsb)                      \
for (                                                             \
    bi = (bt), lsb = (bi + (bs) >= (s)) ? ((s)-bi) : (bs);        \
    bi < (s);                                                     \
    bi += (bs) * (nr), lsb = (bi + (bs) >= (s)) ? ((s)-bi) : (bs) \
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
void print_array(T arr[], uint32_t len);

/**
 * @fn is_sorted
 * @brief Checks whether an array is sorted. Currently sequentially implemented.
 * 
 * @param arr The array of integers to check.
 * @param len Up to which element to check the order (exclusive).
 * 
 * @returns `1` if the array is sorted, elsewise `0`.
**/
bool is_sorted(T arr[], uint32_t len);

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
bool is_uniform(T arr[], uint32_t len, T upper_bound);

#endif  // _CHECKERS_H_