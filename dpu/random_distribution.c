#include <stdint.h>

#include <defs.h>
#include <mram.h>

#include "random_distribution.h"

void generate_uniform_distribution(T __mram_ptr *array, T * const cache,
        mram_range const * const range, T const upper_bound) {
    int is_power_of_two = (upper_bound & (upper_bound - 1)) == 0;
    if (is_power_of_two) {
        size_t i, curr_length, curr_size;
        LOOP_ON_MRAM(i, curr_length, curr_size, (*range)) {
            for (size_t j = 0; j < curr_length; j++) {
                cache[j] = (gen_xs(&rngs[me()]) & (upper_bound - 1));
            }
            mram_write(cache, &array[i], curr_size);
        }
    } else {
        size_t i, curr_length, curr_size;
        LOOP_ON_MRAM(i, curr_length, curr_size, (*range)) {
            for (size_t j = 0; j < curr_length; j++) {
                cache[j] = rr((T)upper_bound, &rngs[me()]);
            }
            mram_write(cache, &array[i], curr_size);
        }
    }
}