#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dpu.h>
#include <dpu_log.h>

#include "common.h"
#include "communication.h"
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
    char binaries[] = BINARIES, *binary = strtok(binaries, ",");
    unsigned found_binaries = 0;
    while (binary != NULL && found_binaries++ != mode) {
        binary = strtok(NULL, ",");
    }
    if (binary == NULL) {
        printf("‘%u’ is no known benchmark ID!\n", mode);
        abort();
    }
    DPU_ASSERT(dpu_alloc(1, NULL, set));
    DPU_ASSERT(dpu_load(*set, binary, NULL));
    DPU_ASSERT(dpu_get_nr_dpus(*set, nr_dpus));
}

int main(int argc, char **argv) {
    struct Params p = input_params(argc, argv);
    struct dpu_set_t set, dpu;
    uint32_t nr_dpus;
    alloc_dpus(&set, &nr_dpus, p.mode);

    struct dpu_arguments host_to_dpu = {
        .length = p.length,
        .basic_seed = 0b1011100111010,
    };

    uint32_t num_of_lengths, num_of_algos;
    uint32_t *lengths = NULL;
    union algo_to_test *algos = NULL;
    DPU_FOREACH(set, dpu) {
        /* Get array of algorithms to test. */
        DPU_ASSERT(dpu_copy_from(dpu, "num_of_algos", 0, &num_of_algos, sizeof(num_of_algos)));
        algos = malloc(sizeof(union algo_to_test[num_of_algos]));
        DPU_ASSERT(dpu_copy_from(dpu, "algos", 0, algos, sizeof(union algo_to_test[num_of_algos])));

        /* Get array of lengths to go through. */
        DPU_ASSERT(dpu_copy_from(dpu, "num_of_lengths", 0, &num_of_lengths, sizeof(num_of_lengths)));
        lengths = malloc(sizeof(uint32_t[num_of_lengths]));
        DPU_ASSERT(dpu_copy_from(dpu, "lengths", 0, lengths, sizeof(uint32_t[num_of_lengths])));
    }

    for (uint32_t li = 0; li < num_of_lengths; li++) {
        // memset(first_moments, 0, nb_of_bytes_for_moments);
        // memset(second_moments, 0, nb_of_bytes_for_moments);

        host_to_dpu.length = lengths[li];
        for (uint32_t rep = 0; rep < p.n_reps; rep++) {
            host_to_dpu.basic_seed += 24;  // The maximum number of threads is 24.
            for (uint32_t id = 0; id < num_of_algos; id++) {
                host_to_dpu.algo_index = id;
                DPU_FOREACH(set, dpu) {
                    DPU_ASSERT(dpu_prepare_xfer(dpu, &host_to_dpu));
                }
                DPU_ASSERT(dpu_push_xfer(set, DPU_XFER_TO_DPU, "host_to_dpu", 0, sizeof(struct dpu_arguments), DPU_XFER_DEFAULT));
                printf("length %d seed %d index %d name %s\n", host_to_dpu.length, host_to_dpu.basic_seed, host_to_dpu.algo_index, algos[id].data.name);
                DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));
            }
        }
    }

    free_dpus(set);
    free(algos);
    free(lengths);

    return EXIT_SUCCESS;
}
