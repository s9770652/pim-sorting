#ifndef _RANDOM_H_
#define _RANDOM_H_

/**
 * @file random.h
 * @brief Uniformly drawing 32-bit and 64-bit integers.
 * 
 * The length of the drawn integers is the same as that of `T`.
 * The integers are drawn using an XorShift generator and rejection sampling
 * since a Research Project by Lukas Geis (Goethe University Frankfurt)
 * has shown them to be extremely performant on the UPMEM architecture.
 * This code draws from the code written by Geis.
**/

#include "../support/common.h"

/**
 * @typedef xorshift
 * @brief A struct that contains a word of state for XorShift generators.
 * Should not be altered manually.
**/
struct xorshift {
    T x;
};

/**
 * @fn seed_xs
 * @brief Sets the initial word of state.
 * @param seed The initial word of state. Must be positive.
 *             More 1s in the binary representation are better.
 * @returns A seeded state.
**/
struct xorshift seed_xs(T seed);

/**
 * @fn gen_xs
 * @brief The XorShift generator generates a 32-bit / 64-bit uniformly drawn random number.
 * @param rng Word of state.
 * @returns A uniformly drawn integer beween `1` and `0xFF...FF`.
**/
T gen_xs(struct xorshift *rng);

/**
 * @fn rr
 * @brief RoundReject uniformly draws an integer by rounding `s` to the next highest power of 2 and using rejection sampling.
 * @param s The upper limit (exclusive) of the range to draw from.
 * @param state The state from which to take the random integer.
 * @returns A uniformly drawn integer between `0` and `s-1`.
**/
T rr(const T s, struct xorshift* state);

#endif  // _RANDOM_H_