#include <stdio.h>

#include <defs.h>
#include <mram.h>

#include "base_sorts.h"
#include "buffers.h"
#include "common.h"
#include "random_generator.h"

// maximum number of elements loaded into MRAM (size must be divisible by 8)
#define LOAD_INTO_MRAM ((1024 * 1024 * 25) >> DIV)
T __mram_noinit input[LOAD_INTO_MRAM];  // array of random numbers
T __mram_noinit output[LOAD_INTO_MRAM];

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
struct xorshift rngs[NR_TASKLETS];

int main() {
    triple_buffers buffers;
    allocate_triple_buffer(&buffers);
    rngs[me()] = seed_with_tasklet_id();

    test_base_sorts(&buffers, &DPU_INPUT_ARGUMENTS);
    return 0;
}