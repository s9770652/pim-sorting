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