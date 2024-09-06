#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dpu.h>
#include <dpu_log.h>

#include "common.h"
#include "communication.h"
#include "params.h"
#include "random_distribution.h"

// Sanity Checks
#if (CACHE_SIZE % DMA_ALIGNMENT)
#error `CACHE_SIZE` is not divisble by `DMA_ALIGNMENT`!
#endif
#if (CACHE_SIZE < DMA_ALIGNMENT)
#error `CACHE_SIZE` too small! The cache must be capable of holding at least `DMA_ALIGNMENT` bytes.
#endif
#if (NR_DPUS != 1)
#error Only one DPU can be used!
#endif
#if (NR_TASKLETS <= 0 || NR_TASKLETS > 16)
#error The number of tasklets must be between 1 and 16!
#endif

/**
 * @brief Frees an allocated set of DPUs.
 * @sa alloc_dpus
 *
 * @param set The set of DPUs to free.
**/
static void free_dpus(struct dpu_set_t set) {
    DPU_ASSERT(dpu_free(set));
}

/**
 * @brief Allocates a set of DPUs and loads the correct binaries.
 * @sa free_dpus
 *
 * @param set The set of DPUs to load.
 * @param mode The mode/benchmark Id passed via the CLI.
**/
static void alloc_dpus(struct dpu_set_t *set, unsigned const mode) {
    char binaries[] = BINARIES, *binary = strtok(binaries, ",");
    unsigned found_binaries = 0;
    while ((binary != NULL) && (found_binaries++ != mode)) {
        binary = strtok(NULL, ",");
    }
    if (binary == NULL) {
        printf("‘%u’ is no known benchmark Id!\n", mode);
        abort();
    }
    DPU_ASSERT(dpu_alloc(1, NULL, set));
    DPU_ASSERT(dpu_load(*set, binary, NULL));
}

/**
 * @brief Counts how many lengths to test have been passed through the CLI.
 * @sa get_lengths
 * 
 * @param lengths List of comma separated values.
 * 
 * @return The number of values passed.
**/
static size_t get_num_of_lengths(char lengths[]) {
    size_t nr_of_lengths = 1;
    for (size_t i = 0; lengths[i]; i++)
        nr_of_lengths += (lengths[i] == ',');
    return nr_of_lengths;
}

/**
 * @brief Returns a pointer to an array of all lengths passed through the CLI.
 * @sa get_num_of_lengths
 * 
 * @param lengths List of comma separated values.
 * @param nr_of_lengths The number of CSV in the list.
 * 
 * @return An array of all lengths.
**/
static uint32_t *get_lengths(char lengths[], size_t nr_of_lengths) {
    uint32_t *ns = malloc(sizeof(uint32_t[nr_of_lengths]));
    char *lengths_to_read = malloc(strlen(lengths));  // Oh …
    memcpy(lengths_to_read, lengths, strlen(lengths));  // … my …
    char *n = strtok(lengths_to_read, ",");  // … God!
    for (size_t i = 0; i < nr_of_lengths; i++) {
        ns[i] = (uint32_t)atof(n);
        if (ns[i] == 0) {
            printf("‘%s’ is not a valid length!\n", n);
            abort();
        }
        n = strtok(NULL, ",");
    }
    return ns;
}

/**
 * @brief Launches the program to execute the current sorting function once.
 * 
 * @param set The set with the DPU.
 * @param host_to_dpu The input data to send to the DPU.
 * @param dpu_to_host What the DPU has sent so far.
**/
static void test(struct dpu_set_t *set, struct dpu_arguments *host_to_dpu,
        struct dpu_results *dpu_to_host) {
    struct dpu_set_t dpu;
    struct dpu_results new_result;
    DPU_FOREACH(*set, dpu) {
        DPU_ASSERT(dpu_copy_to(dpu, "host_to_dpu", 0, host_to_dpu, sizeof *host_to_dpu));
        DPU_ASSERT(dpu_launch(*set, DPU_SYNCHRONOUS));
        DPU_ASSERT(dpu_copy_from(dpu, "dpu_to_host", 0, &new_result, sizeof new_result));
        // DPU_ASSERT(dpu_log_read(dpu, stdout));
    }
    dpu_to_host->firsts += new_result.firsts;
    dpu_to_host->seconds += new_result.seconds;
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
        "# reps=%u, dist name=%s, dist param=%"T_QUALIFIER", TYPE=%s, CACHE_SIZE=%d, "
        "SEQREAD_CACHE_SIZE=%d, NR_TASKLETS=%d, CALL_OVERHEAD=%u\n# %s\n",
        params->n_reps,
        get_dist_name(params->dist_type),
        params->dist_param,
        TYPE_NAME,
        CACHE_SIZE,
        SEQREAD_CACHE_SIZE,
        NR_TASKLETS,
        CALL_OVERHEAD,
        TABLE_HEADER
    );
    printf("n");
    for (size_t i = 0; i < num_of_algos; i++)
        printf("\tµ_%s σ_%s", algos[i].data.name, algos[i].data.name);
    printf("\n");
}

/**
 * @brief Prints the average of the measured runtimes and their standard deviation to the console.
 * Additionally prints the average on a per-element basis if not using the file-friendly format.
 * 
 * @param num_of_algos The number of sorting algorithms measured.
 * @param length The number of input elements which were sorted.
 * @param reps How often each test was repeated.
 * @param dpu_to_host The results as measured by the DPUs.
**/
static void print_measurements(size_t const num_of_algos, size_t const length, time const reps,
        struct dpu_results dpu_to_host[]) {
    printf("%-4zd", length);
    for (size_t id = 0; id < num_of_algos; id++) {
        time mean = get_mean_of_time(reps, dpu_to_host[id].firsts);
        time std = get_std_of_time(reps, dpu_to_host[id].firsts, dpu_to_host[id].seconds);
        printf("\t%7lu %5lu", mean, std);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    struct Params p = input_params(argc, argv);
    struct dpu_set_t set, dpu;
    alloc_dpus(&set, p.mode);

    /* Read in test data. */
    uint32_t num_of_algos;
    union algo_to_test *algos = NULL;
    DPU_FOREACH(set, dpu) {
        /* Get array of algorithms to test. */
        DPU_ASSERT(dpu_copy_from(dpu, "num_of_algos", 0, &num_of_algos, sizeof(num_of_algos)));
        algos = malloc(sizeof(union algo_to_test[num_of_algos]));
        DPU_ASSERT(dpu_copy_from(dpu, "algos", 0, algos, sizeof(union algo_to_test[num_of_algos])));
    }

    /* Set up tests. */
    T * const input = malloc(sizeof(T[LOAD_INTO_MRAM]));
    struct dpu_results *dpu_to_host = malloc(sizeof(struct dpu_results[num_of_algos]));
    struct dpu_arguments host_to_dpu = {
        .basic_seed = 0b1011100111010,
    };
    srand((unsigned)1961071919591017);

    size_t num_of_lengths = get_num_of_lengths(p.lengths);
    uint32_t *lengths = get_lengths(p.lengths, num_of_lengths);

    /* Perform tests. */
    print_header(algos, num_of_algos, &p);
    for (uint32_t li = 0; li < num_of_lengths; li++) {
        uint32_t const len = lengths[li], offset = DMA_ALIGNED(len * sizeof(T)) / sizeof(T);
        host_to_dpu.length = len;
        host_to_dpu.offset = offset;
        host_to_dpu.part_length = DMA_ALIGNED(DIV_CEIL(len, NR_TASKLETS) * sizeof(T)) / sizeof(T);
        uint32_t const reps_per_launch = LOAD_INTO_MRAM / len;
        if (len > LOAD_INTO_MRAM) {
            printf("The input length %u is too big! The maximum is %u.\n", len, LOAD_INTO_MRAM);
            abort();
        }

        memset(dpu_to_host, 0, sizeof(struct dpu_results[num_of_algos]));
        for (uint32_t rep = 0; rep < p.n_reps; rep += reps_per_launch) {
            host_to_dpu.reps = (reps_per_launch > p.n_reps - rep) ? p.n_reps - rep : reps_per_launch;

            for (uint32_t i = 0; i < host_to_dpu.reps; i++) {
                generate_input_distribution(&input[i * offset], len, p.dist_type, p.dist_param);
            }
            size_t const transferred = DMA_ALIGNED(sizeof(T[offset * host_to_dpu.reps]));
            DPU_ASSERT(dpu_copy_to(dpu, "input", 0, input, transferred));

            for (uint32_t id = 0; id < num_of_algos; id++) {
                host_to_dpu.algo_index = id;
                test(&set, &host_to_dpu, &dpu_to_host[id]);
            }
            host_to_dpu.basic_seed += host_to_dpu.reps * NR_TASKLETS;
        }
        print_measurements(num_of_algos, len, p.n_reps, dpu_to_host);
    }

    /* Clean up. */
    free_dpus(set);
    free(algos);
    free(lengths);
    free(input);
    free(dpu_to_host);

    return EXIT_SUCCESS;
}
