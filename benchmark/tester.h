#ifndef _TESTER_H_
#define _TESTER_H_

#include "buffers.h"
#include "common.h"
#include "random_generator.h"

/// @brief Every WRAM sorting functions must adher to this pattern.
typedef void base_sort_algo(T *, T *);

/**
 * @brief Holds both a pointer to and the name of a function.
 * Useful for creating lists of functions to test.
**/
struct algo_to_test {
    /// @brief A pointer to a WRAM sorting function.
    base_sort_algo *algo;
    /// @brief The name of a WRAM sorting function.
    char name[14];
};

/**
 * @brief Runs a set of algorithms and prints their runtimes.
 * 
 * @param name The name of the test to be shown in the console.
 * @param algos A list of algorithms and their names.
 * @param num_of_algos The length of the algorithm list.
 * @param lengths A list of input lengths on which to run the algorithms.
 * @param num_of_lengths The length of the length list.
 * @param buffers A struct containing a WRAM cache.
 * @param args The arguments with which the program was started,
 * including the number of repetitions and the upper bound for random numbers.
**/
void test_algos(char const name[], struct algo_to_test const algos[], size_t num_of_algos,
        size_t const lengths[], size_t num_of_lengths, triple_buffers const *buffers,
        struct dpu_arguments const *args);

/// @brief The state of the generator used for drawing a pivot element.
extern struct xorshift_offset pivot_rng_state;

#define END (0)
#define MIDDLE (1)
#define MEDIAN_OF_THREE (2)
#define RANDOM (3)
#define PIVOT MEDIAN_OF_THREE

/**
 * @brief Returns a pivot element for a WRAM array.
 * Used by QuickSort.
 * Currently, the method of choosing must be changed by (un-)commenting the respective code lines.
 * Possible are:
 * - always the rightmost element
 * - the median of the leftmost, middle and rightmost element
 * - a random element
 * 
 * @param start The first element of the WRAM array to sort.
 * @param end The last element of said array.
 *
 * @return The pivot element.
**/
static inline T *get_pivot(T const * const start, T const * const end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
#if (PIVOT == END)
    /* Always the rightmost element. */
    return (T *)end;
#elif (PIVOT == MIDDLE)
    /* Always the middle element. */
    return (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
#elif (PIVOT == MEDIAN_OF_THREE)
    /* The median of the leftmost, middle and rightmost element. */
    T const * const middle = (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
    if ((*start > *middle) ^ (*start > *end))
        return (T *)start;
    else if ((*start > *middle) ^ (*end > *middle))
        return (T *)middle;
    else
        return (T *)end;
#elif (PIVOT == RANDOM)
    /* Pick a random element. */
    size_t const n = end - start;
    size_t const offset = rr_offset(n, &pivot_rng_state);
    return (T *)(start + offset);
#endif
}

#endif  // _TESTER_H_
