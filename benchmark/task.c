#include <stdio.h>
#include <stdlib.h>

#include <defs.h>
#include <perfcounter.h>

#include "buffers.h"
#include "common.h"
#include "random_generator.h"

#include "base_sorts.h"
#include "quick_sorts.h"

typedef void test_function(triple_buffers *, struct dpu_arguments *);

// maximum number of elements loaded into MRAM (size must be divisible by 8)
#define LOAD_INTO_MRAM ((1024 * 1024 * 25) >> DIV)
T __mram_noinit input[LOAD_INTO_MRAM];  // array of random numbers
T __mram_noinit output[LOAD_INTO_MRAM];

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;

int main(void) {
    triple_buffers buffers;
    allocate_triple_buffer(&buffers);
    perfcounter_config(COUNT_CYCLES, false);

    if (DPU_INPUT_ARGUMENTS.mode == 0) {  // called via debugger?
        DPU_INPUT_ARGUMENTS.mode = 2;
        DPU_INPUT_ARGUMENTS.n_reps = 10;
        DPU_INPUT_ARGUMENTS.upper_bound = 4;
    }
    test_function *testers[] = {
        NULL, test_wram_sorts, test_very_small_sorts, test_quick_sorts
    };
    if (DPU_INPUT_ARGUMENTS.mode > (sizeof testers / sizeof testers[0])) {
        printf("‘%u’ is no known benchmark ID!\n", DPU_INPUT_ARGUMENTS.mode);
        return EXIT_SUCCESS;
    }
    testers[DPU_INPUT_ARGUMENTS.mode](&buffers, &DPU_INPUT_ARGUMENTS);
    return EXIT_SUCCESS;
}
