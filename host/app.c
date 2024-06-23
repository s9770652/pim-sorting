#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dpu.h>
#include <dpu_log.h>

#include "common.h"
#include "communication.h"
#include "params.h"
#include "pivot.h"
#include "random_distribution.h"

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
    while ((binary != NULL) && (found_binaries++ != mode)) {
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

/**
 * @brief Launches the program to execute the current sorting function once.
 * 
 * @param set The set with the DPU.
 * @param host_to_dpu The input data to send to the DPU.
 * @param firsts The sum of the first moments of the current sorting function.
 * @param seconds The sum of the second moments of the current sorting function.
 * @param input The data to be sorted by the DPU.
**/
static void test(struct dpu_set_t *set, struct dpu_arguments *host_to_dpu, time *firsts,
        time *seconds, T input[]) {
    struct dpu_set_t dpu;
    time new_result;
    DPU_FOREACH(*set, dpu) {
        DPU_ASSERT(dpu_copy_to(dpu, "host_to_dpu", 0, host_to_dpu, sizeof *host_to_dpu));
        DPU_ASSERT(dpu_copy_to(dpu, "input", 0, input, ROUND_UP_POW2(sizeof(T[host_to_dpu->length]), 8)));
        DPU_ASSERT(dpu_launch(*set, DPU_SYNCHRONOUS));
        DPU_ASSERT(dpu_copy_from(dpu, "dpu_to_host", 0, &new_result, sizeof new_result));
    }
    *firsts += new_result;
    *seconds += new_result * new_result;
}

/**
 * @brief The arithmetic mean of measured times.
 * 
 * @param zeroth The zeroth moment, that is, the number of measurements.
 * @param first The first moment, that is, the sum of measured times.
 * 
 * @return The mean time.
**/
static time get_mean_of_time(time const zeroth, time const first) {
    return first / zeroth;
}

/**
 * @brief The standard deviation of measured times.
 * 
 * @param zeroth The zeroth moment, that is, the number of measurements.
 * @param first The first moment, that is, the sum of measured times.
 * @param second The second moment, that is, the sum of squared measured times.
 * 
 * @return The standard deviation.
**/
static time get_std_of_time(time const zeroth, time const first, time second) {
    if (zeroth == 1) return 0;
    return sqrt((zeroth * second - first * first) / (zeroth * (zeroth - 1)));
}

/**
 * @brief Prints the name of the test run and either a legend or column names.
 * 
 * @param algos A list of sorting algorithms and their names.
 * @param num_of_algos The length of the list.
 * @param args The arguments with which the program was started,
 * including the number of repetitions and the upper bound for random numbers.
**/
static void print_header(union algo_to_test const algos[], size_t const num_of_algos,
        struct Params *params) {
    printf(
        "# reps=%u, upper bound=%"T_QUALIFIER", DIST=%s, PIVOT=%s, TYPE=%s, BLOCK_SIZE=%d, "
        "SEQREAD_CACHE_SIZE=%d, NR_TASKLETS=%d, overhead=%u\n",
        params->n_reps,
        params->dist_param,
        get_dist_name(params->dist_type),
        PIVOT_NAME,
        TYPE_NAME,
        BLOCK_SIZE,
        SEQREAD_CACHE_SIZE,
        NR_TASKLETS,
        CALL_OVERHEAD
    );
    printf("n\t");
    for (size_t i = 0; i < num_of_algos; i++)
        printf("µ_%s σ_%s\t", algos[i].data.name, algos[i].data.name);
    printf("\n");
}

/**
 * @brief Prints the average of the measured runtimes and their standard deviation to the console.
 * Additionally prints the average on a per-element basis if not using the file-friendly format.
 * 
 * @param num_of_algos The number of sorting algorithms measured.
 * @param length The number of input elements which were sorted.
 * @param zeroth The zeroth moment (universal for all algorithms).
 * @param firsts The measured first moments of all algorithms.
 * @param seconds The measured second moments of all algorithms.
**/
static void print_measurements(size_t const num_of_algos, size_t const length, time const zeroth,
        time const firsts[], time const seconds[]) {
    printf("%zd\t", length);
    for (size_t id = 0; id < num_of_algos; id++) {
        time mean = get_mean_of_time(zeroth, firsts[id]);
        time std = get_std_of_time(zeroth, firsts[id], seconds[id]);
        printf("%6lu %5lu\t", mean, std);
    }
    printf("\n");
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

    T *input = malloc(LOAD_INTO_MRAM * sizeof(T));
    time *firsts = malloc(sizeof(time[num_of_algos]));  // sums of measured times
    time *seconds = malloc(sizeof(time[num_of_algos]));  // sums of squares of measured times

    srand((unsigned)1961071919591017);
    print_header(algos, num_of_algos, &p);
    for (uint32_t li = 0; li < num_of_lengths; li++) {
        memset(firsts, 0, sizeof(time[num_of_algos]));
        memset(seconds, 0, sizeof(time[num_of_algos]));
        host_to_dpu.length = lengths[li];
        for (uint32_t rep = 0; rep < p.n_reps; rep++) {
            host_to_dpu.basic_seed += NR_TASKLETS;
            generate_input_distribution(input, lengths[li], p.dist_type, p.dist_param);
            for (uint32_t id = 0; id < num_of_algos; id++) {
                host_to_dpu.algo_index = id;
                test(&set, &host_to_dpu, &firsts[id], &seconds[id], input);
            }
        }
        print_measurements(num_of_algos, lengths[li], p.n_reps, firsts, seconds);
    }

    // generate_input_distribution(input, 240, p.dist_type, p.dist_param);
    // for (size_t i = 0; i < 240; i++)
    //     printf("%d ", input[i]);
    // printf("\n");

    free_dpus(set);
    free(algos);
    free(lengths);
    free(input);
    free(firsts);
    free(seconds);

    return EXIT_SUCCESS;
}
