#include <stdbool.h>
#include <stdint.h>

#include <defs.h>
#include <mram.h>

#include "random_distribution.h"

void generate_uniform_distribution(T * const cache, size_t length, T const upper_bound) {
    bool const is_power_of_two = (upper_bound & (upper_bound - 1)) == 0;
    if (is_power_of_two) {
        for (size_t j = 0; j < length; j++) {
            cache[j] = (gen_xs(&rngs[me()]) & (upper_bound - 1));
        }
    } else {
        for (size_t j = 0; j < length; j++) {
            cache[j] = rr((T)upper_bound, &rngs[me()]);
        }
    }
}

void generate_uniform_distribution_mram(T __mram_ptr *array, T * const cache,
        mram_range const * const range, T const upper_bound) {
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, (*range)) {
        generate_uniform_distribution(cache, curr_length, upper_bound);
        mram_write(cache, &array[i], curr_size);
    }
}