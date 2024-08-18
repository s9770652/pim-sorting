/**
 * @file
 * @brief Checking the sanity of generated and sorted numbers.
 * 
 * The use of some of these functions is intented for development purposes only.
**/

#ifndef _CHECKERS_H_
#define _CHECKERS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "common.h"
#include "mram_loop.h"

/**
 * @brief Prints an MRAM array in lines of `MAX_TRANSFER_LENGTH_TRIPLE` numbers.
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

/**
 * @brief Contains several statistical values about MRAM arrays.
 * @internal `sorted` may be more intuitive than `unsorted`.
 * This way, however, the default initial value, 0, is more convenient
 * as tasklets can abort prematurely without messing up the logic of the other tasklets.
**/
typedef struct array_stats
{
    /// @brief The sum of all elements in some array.
    uint64_t sum;
    /// @brief The counts of the values in the range `[0, 7]`.
    size_t counts[8];
    /// @brief Whether the array is sorted.
    bool unsorted;
} array_stats;

#if (!CHECK_SANITY)
    static inline
#endif  // !CHECK_SANITY
/**
 * @brief Calcucates the sum and gets the value counts of an MRAM array.
 * 
 * @param array The MRAM array to check.
 * @param cache A cache in WRAM.
 * @param range The range of indices for the calling tasklet to check.
 * @param dummy Whether a dummy variable was set.
 * If present, it is excluded from the statistics.
 * @param result The struct where the results are stored.
 * The value for `unsorted` is undefined.
**/
void get_stats_unsorted(T __mram_ptr const *array, T *cache, mram_range range,
        bool dummy, array_stats *result)
#if (!CHECK_SANITY)
    { (void)array, (void)cache, (void)range, (void)dummy, (void)result; }
#endif  // !CHECK_SANITY
;

#if (!CHECK_SANITY)
    static inline
#endif  // !CHECK_SANITY
/**
 * @brief Calulcates the sum and gets the value counts of an MRAM array.
 * Also checks whether the array is sorted.
 * 
 * @param array The MRAM array to check.
 * @param cache A cache in WRAM.
 * @param range The range of indices for the calling tasklet to check.
 * @param dummy Whether a dummy variable was set.
 * If present, it is excluded from the statistics.
 * @param result The struct where the results are stored.
**/
void get_stats_sorted(T __mram_ptr const *array, T *cache, mram_range range,
        bool dummy, array_stats *result)
#if (!CHECK_SANITY)
    { (void)array, (void)cache, (void)range, (void)dummy, (void)result; }
#endif  // !CHECK_SANITY
;

#if (!CHECK_SANITY)
    static inline
#endif  // !CHECK_SANITY
/**
 * @brief Calcucates the sum and gets the value counts of a WRAM array.
 * 
 * @param array The WRAM array to check.
 * @param length The number of elements in the array.
 * @param result The struct where the results are stored.
 * The value for `unsorted` is undefined.
**/
void get_stats_unsorted_wram(T const array[], size_t length, array_stats *result)
#if (!CHECK_SANITY)
    { (void)array, (void)length, (void)result; }
#endif  // !CHECK_SANITY
;

#if (!CHECK_SANITY)
    static inline
#endif  // !CHECK_SANITY
/**
 * @brief Calulcates the sum and gets the value counts of a WRAM array.
 * Also checks whether the array is sorted.
 * 
 * @param array The WRAM array to check.
 * @param length The number of elements in the array.
 * @param result The struct where the results are stored.
**/
void get_stats_sorted_wram(T const array[], size_t length, array_stats *result)
#if (!CHECK_SANITY)
    { (void)array, (void)length, (void)result; }
#endif  // !CHECK_SANITY
;

#if (!CHECK_SANITY)
    static inline
#endif  // !CHECK_SANITY
/**
 * @brief Compares two given stats and prints appropriate messages.
 * 
 * @param stats_unsorted The statistics of the unsorted array.
 * @param stats_sorted The statistics of the sorted array.
 * @param bool Whether a message is to be printed if no problem was detected.
 * 
 * @return `EXIT_FAILURE` if a problem was detected, else `EXIT_SUCCESS`.
**/
bool compare_stats(array_stats const *stats_unsorted, array_stats const *stats_sorted,
        bool print_on_success)
#if (!CHECK_SANITY)
    { (void)stats_unsorted, (void)stats_sorted, (void)print_on_success; return EXIT_SUCCESS; }
#endif  // !CHECK_SANITY
;

/**
 * @brief Checks the mean and variance of an array of integers
 * and compares them with an ideal uniform distribution.
 * @note Currently sequentially implemented.
 * @warning This function allocates `upper_bound` × `sizeof(T *)` bytes.
 * 
 * @param array The array of integers to check.
 * @param length Up to which element to check the mean and variance.
 * @param upper_bound The upper bound (exclusive) of the range whence the random numbers are drawn.
 * 
 * @returns `true` if the mean and variance deviate at most 10 % from the ideal, elsewise `false`.
**/
bool is_uniform(T *array, size_t length, T upper_bound);

#endif  // _CHECKERS_H_
