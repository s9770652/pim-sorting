/**
 * @file
 * @brief Uniformly drawing 32-bit and 64-bit integers.
 * 
 * The length of the drawn integers is the same as that of `T`.
 * The integers are drawn using an XorShift generator and rejection sampling
 * since a Research Project by Lukas Geis (Goethe University Frankfurt)
 * has shown them to be extremely performant on the UPMEM architecture.
 * This code draws from the code written by Geis.
**/

#ifndef _RANDOM_H_
#define _RANDOM_H_

#include <assert.h>

#include "common.h"

/**
 * @brief A struct that contains a word of state for XorShift generators.
 * Should not be altered manually.
**/
struct xorshift {
    T x;
};

/**
 * @brief Sets the initial word of state.
 * 
 * @param seed The initial word of state. Must be positive.
 * More 1s in the binary representation are better.
 * 
 * @returns A seeded state.
**/
static inline struct xorshift seed_xs(T const seed) {
    assert(seed > 0);
    struct xorshift rng = { seed };
    return rng;
}

/**
 * @brief The XorShift generator generates a 32-bit / 64-bit uniformly drawn random number.
 * 
 * @param rng Word of state.
 * 
 * @returns A uniformly drawn integer beween `1` and `0xFF...FF`.
**/
static inline T gen_xs(struct xorshift *rng) {
    T x = rng->x;
#ifdef UINT32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
#else
    x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
#endif
    return rng->x = x;
}

/**
 * @brief RoundReject uniformly draws an integer
 * by rounding `s` to the next highest power of 2 and using rejection sampling.
 * 
 * @param s The upper limit (exclusive) of the range to draw from.
 * @param state The state from which to take the random integer.
 * 
 * @returns A uniformly drawn integer between `0` and `s-1`.
**/
static inline T rr(T const s, struct xorshift *state) {
#ifdef UINT32
    T mask = (1 << (32 - __builtin_clz(s))) - 1;
#else
    T mask = (1 << (64 - __builtin_clzl(s))) - 1;
#endif
    T x = gen_xs(state) & mask;
    while (x >= s) {
        x = gen_xs(state) & mask;
    }
    return x;
}

#endif  // _RANDOM_H_