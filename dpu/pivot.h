/**
 * @file
 * @brief Drawing a pivot element for partitioning.
**/

#ifndef _PIVOT_H_
#define _PIVOT_H_

#include "common.h"
#include "random_generator.h"

#if (!(defined(END) || defined(MIDDLE) || defined(MEDIAN) || defined(RANDOM)))
#error Unknown pivot choice!
#endif

/// @brief The state of the generator used for drawing a pivot element.
extern struct xorshift_offset pivot_rng_state;

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
#if defined(END)
    /* Always the rightmost element. */
    return (T *)end;
#elif defined(MIDDLE)
    /* Always the middle element. */
    return (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
#elif defined(MEDIAN)
    /* The median of the leftmost, middle, and rightmost element. */
    T const * const middle = (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
    if ((*start > *middle) ^ (*start > *end))
        return (T *)start;
    else if ((*start > *middle) ^ (*end > *middle))
        return (T *)middle;
    else
        return (T *)end;
#elif defined(RANDOM)
    /* Pick a random element. */
    size_t const n = end - start;
    size_t const offset = rr_offset(n, &pivot_rng_state);
    return (T *)(start + offset);
#endif
}

#endif  // _PIVOT_H_
