/**
 * @file
 * @brief Drawing a pivot element for partitioning.
**/

#ifndef _PIVOT_H_
#define _PIVOT_H_

#include "common.h"

/// @brief The state of the generator used for drawing a pivot element.
extern struct xorshift_offset pivot_rng_state;

#define END (0)
#define MIDDLE (1)
#define MEDIAN_OF_THREE (2)
#define RANDOM (3)
#define PIVOT MEDIAN_OF_THREE

#if (PIVOT == END)
#define PIVOT_NAME "end"
#elif (PIVOT == MIDDLE)
#define PIVOT_NAME "middle"
#elif (PIVOT == MEDIAN_OF_THREE)
#define PIVOT_NAME "median of three"
#elif (PIVOT == RANDOM)
#define PIVOT_NAME "random"
#endif

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
    /* The median of the leftmost, middle, and rightmost element. */
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

#endif  // _PIVOT_H_
