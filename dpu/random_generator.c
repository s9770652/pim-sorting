#include <assert.h>
#include <stdio.h>

#include <defs.h>
#include <mram.h>
#include <sysdef.h>

#include "random_generator.h"

/**
 * @brief Sets the initial word of state.
 * 
 * @param seed The initial word of state. Must be positive.
 * More 1s in the binary representation are better.
 * 
 * @returns A seeded state.
**/
struct xorshift seed_xs(T const seed) {
    assert(seed > 0);
    struct xorshift rng = { seed };
    return rng;
}

struct xorshift seed_with_tasklet_id(void) {
    return seed_xs(me() + 0b100111010);  // arbitrary number to improve the seed
}

T gen_xs(struct xorshift *rng) {
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

T rr(T const s, struct xorshift *state) {
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