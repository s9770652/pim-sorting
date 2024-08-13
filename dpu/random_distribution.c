#include <stdbool.h>
#include <stdint.h>

#include <defs.h>
#include <mram.h>

#include "buffers.h"
#include "dpu_math.h"
#include "random_distribution.h"
#include "random_generator.h"

void generate_uniform_distribution_wram(T * const start, T * const end, T const upper_bound) {
    bool const is_power_of_two = (upper_bound & (upper_bound - 1)) == 0;
    if (upper_bound == 0) {
        for (T *t = start; t <= end; t++) {
            *t = gen_xs(&input_rngs[me()]);
        }
    } else if (is_power_of_two) {
        for (T *t = start; t <= end; t++) {
            *t = gen_xs(&input_rngs[me()]) & (upper_bound - 1);
        }
    } else {
        for (T *t = start; t <= end; t++) {
            *t = rr(upper_bound, &input_rngs[me()]);
        }
    }
}

void generate_uniform_distribution_mram(T __mram_ptr *array, T * const cache,
        mram_range const * const range, T const upper_bound) {
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, (*range)) {
        generate_uniform_distribution_wram(cache, &cache[curr_length-1], upper_bound);
        mram_write_triple(cache, &array[i], curr_size);
    }
}

void generate_sorted_distribution_wram(T * const start, T * const end) {
    T counter = T_MIN;
    for (T *t = start; t <= end; t++) {
        *t = counter++;
    }
}

void generate_sorted_distribution_mram(T __mram_ptr *array, T * const cache,
        mram_range const * const range) {
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, (*range)) {
        generate_sorted_distribution_wram(cache, &cache[curr_length-1]);
        mram_write_triple(cache, &array[i], curr_size);
    }
}

void generate_reverse_sorted_distribution_wram(T * const start, T * const end) {
    T counter = T_MIN;
    for (T *t = end; t >= start; t--) {
        *t = counter++;
    }
}

void generate_reverse_sorted_distribution_mram(T __mram_ptr *array, T * const cache,
        mram_range const * const range) {
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, (*range)) {
        generate_reverse_sorted_distribution_wram(cache, &cache[curr_length-1]);
        mram_write_triple(cache, &array[i], curr_size);
    }
}

void generate_almost_sorted_distribution_wram(T * const start, T * const end, size_t swaps) {
    generate_sorted_distribution_wram(start, end);
    size_t const n = end - start + 1;
    swaps = (swaps) ? : sqroot_on_dpu(n);
    for (size_t s = 0; s < swaps; s++) {
        size_t const i = rr(n - 1, &input_rngs[me()]);
        size_t j;
        do { j = rr(n - 1, &input_rngs[me()]); } while (i == j);
        swap(&start[i], &start[j]);
    }
}
