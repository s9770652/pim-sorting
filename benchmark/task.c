#include <stdio.h>

#include <defs.h>
#include <mram.h>
#include <perfcounter.h>

#include "base_sorts.h"
#include "buffers.h"
#include "common.h"
#include "random_generator.h"

typedef void test_function(triple_buffers *, struct dpu_arguments *);

// maximum number of elements loaded into MRAM (size must be divisible by 8)
#define LOAD_INTO_MRAM ((1024 * 1024 * 25) >> DIV)
T __mram_noinit input[LOAD_INTO_MRAM];  // array of random numbers
T __mram_noinit output[LOAD_INTO_MRAM];

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
struct xorshift rngs[NR_TASKLETS];

int main(void) {
    triple_buffers buffers;
    allocate_triple_buffer(&buffers);
    rngs[me()] = seed_with_tasklet_id();
    perfcounter_config(COUNT_CYCLES, false);

    if (DPU_INPUT_ARGUMENTS.mode == 0) {  // called via debugger?
        DPU_INPUT_ARGUMENTS.mode = 2;
        DPU_INPUT_ARGUMENTS.n_reps = 10;
        DPU_INPUT_ARGUMENTS.upper_bound = 4;
    }
    test_function *testers[] = {
        NULL, test_wram_sorts, test_custom_shell_sorts
    };
    if (DPU_INPUT_ARGUMENTS.mode > (sizeof testers / sizeof testers[0])) {
        printf("'%d' is no known benchmark ID!\n", DPU_INPUT_ARGUMENTS.mode);
        return 0;
    }
    testers[DPU_INPUT_ARGUMENTS.mode](&buffers, &DPU_INPUT_ARGUMENTS);
    return 0;
}
