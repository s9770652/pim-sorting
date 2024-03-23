#include <stdio.h>
#include <stdlib.h>

#include <dpu.h>
#include <dpu_log.h>

#include "common.h"
#include "params.h"

// Sanity Checks
#if (BLOCK_SIZE > 2048)
#error `BLOCK_SIZE` too big! `mram_read` and `mram_write` can transfer at most 2048 bytes.
#endif
#if (BLOCK_SIZE < 8)
#error `BLOCK_SIZE` too small! `mram_read` and `mram_write` must transfer at least 8 bytes.
#endif
#if (BLOCK_SIZE % 8)
#error `BLOCK_SIZE` is not divisble by eight! Accesses to MRAM must be aligned on 8 bytes.
#endif
#if (NR_DPUS <= 0)
#error The number of DPUs must be positive!
#endif
#if (NR_TASKLETS <= 0 || NR_TASKLETS > 16)
#error The number of tasklets must be between 1 and 16!
#endif

static void free_dpus(struct dpu_set_t set) {
    DPU_ASSERT(dpu_free(set));
}

static void alloc_dpus(struct dpu_set_t *set, uint32_t *nr_dpus, unsigned const mode) {
    char *binary = mode ? BENCHMARK_BINARY : SORTING_BINARY;
    DPU_ASSERT(dpu_alloc(1, NULL, set));
    DPU_ASSERT(dpu_load(*set, binary, NULL));
    DPU_ASSERT(dpu_get_nr_dpus(*set, nr_dpus));
}

int main(int argc, char** argv) {
    struct Params p = input_params(argc, argv);
    struct dpu_set_t set, dpu;
    uint32_t nr_dpus;
    alloc_dpus(&set, &nr_dpus, p.mode);

    struct dpu_arguments input_arguments = {
        .length = p.length,
        .upper_bound = p.upper_bound,
        .n_reps = p.n_reps,
        .n_warmup = p.n_warmup
    };
    DPU_FOREACH(set, dpu) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, &input_arguments));
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0, sizeof(struct dpu_arguments), DPU_XFER_DEFAULT));  // broadcast?

    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    DPU_FOREACH(set, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }

    free_dpus(set);

    return 0;
}