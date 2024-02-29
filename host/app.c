#include <stdio.h>
#include <stdlib.h>

#include <dpu.h>
#include <dpu_log.h>

#include "../support/common.h"
#include "../support/params.h"

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

static void free_dpus(struct dpu_set_t set) {
    DPU_ASSERT(dpu_free(set));
}

static void alloc_dpus(struct dpu_set_t *set, uint32_t *nr_dpus) {
    DPU_ASSERT(dpu_alloc(1, NULL, set));
    DPU_ASSERT(dpu_load(*set, DPU_BINARY, NULL));
    DPU_ASSERT(dpu_get_nr_dpus(*set, nr_dpus));
}

int main(int argc, char** argv) {
    struct dpu_set_t set, dpu;
    uint32_t nr_dpus;
    alloc_dpus(&set, &nr_dpus);

    struct Params p = input_params(argc, argv);
    struct dpu_arguments input_arguments = {
        p.length,
        p.upper_bound
    };
    DPU_FOREACH(set, dpu) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, &input_arguments));
    }
    DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0, sizeof(input_arguments), DPU_XFER_DEFAULT));  // broadcast?

    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    DPU_FOREACH(set, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }

    free_dpus(set);

    return 0;
}