#include <stdio.h>
#include <string.h>

#include <alloc.h>
#include <attributes.h>
#include <perfcounter.h>

#include "dpu_math.h"
#include "random_distribution.h"
#include "random_generator.h"

#include "tester.h"

struct xorshift rngs[NR_TASKLETS];
struct xorshift pivot_rng_state;

/**
 * @brief An empty functions used when calculating the function call overhead.
 * 
 * @param start Any WRAM address.
 * @param end Any WRAM address.
**/
__noinline static void empty_sort(T *start, T *end) { (void)start; (void)end; }

/**
 * @brief The arithmetic mean of measured times.
 * 
 * @param zeroth The zeroth moment, that is, the number of measurements.
 * @param first The first moment, that is, the sum of measured times.
 * 
 * @return The mean time.
**/
static perfcounter_t get_mean_of_time(perfcounter_t const zeroth, perfcounter_t const first) {
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
static perfcounter_t get_std_of_time(perfcounter_t const zeroth, perfcounter_t const first,
        perfcounter_t second) {
    if (zeroth == 1) return 0;
    return sqroot_on_dpu((zeroth * second - first * first) / (zeroth * (zeroth - 1)));
}

#ifndef PRINT_IN_FILE_FRIENDLY_FORMAT
#define PRINT_IN_FILE_FRIENDLY_FORMAT (0)
#endif

/**
 * @brief Prints the name of the test run and either a legend or column names.
 * 
 * @param name The name of the test.
 * @param algos A list of sorting algorithms and their names.
 * @param num_of_algos The length of the list.
 * @param args The arguments with which the program was started,
 * including the number of repetitions and the upper bound for random numbers.
**/
static void print_header(char const name[], struct algo_to_test const algos[],
        size_t const num_of_algos, struct dpu_arguments const * const args) {
    printf(
        "# reps=%d, upper bound=%d, TYPE=%s, BLOCK_SIZE=%d, SEQREAD_CACHE_SIZE=%d, NR_TASKLETS=%d\n",
        args->n_reps,
        args->upper_bound,
        TYPE_NAME,
        BLOCK_SIZE,
        SEQREAD_CACHE_SIZE,
        NR_TASKLETS
    );
#if PRINT_IN_FILE_FRIENDLY_FORMAT
    (void)name;
    printf("n\t");
    for (size_t i = 0; i < num_of_algos; i++)
        printf("µ_%s σ_%s\t", algos[i].name, algos[i].name);
#else
    (void)algos;
    (void)num_of_algos;
    printf("TEST: %s (C: cycles, n: length)\n", name);
#endif
    printf("\n");
}

/**
 * @brief Prints the average of the measured runtimes and their standard deviation to the console.
 * Additionally prints the average on a per-element basis if not using the file-friendly format.
 * 
 * @param algos A list of sorting algorithms measured.
 * @param num_of_algos The number of sorting algorithms measured.
 * @param length The number of input elements which were sorted.
 * @param zeroth The zeroth moment (universal for all algorithms).
 * @param firsts The measured first moments of all algorithms.
 * @param seconds The measured second moments of all algorithms.
**/
static void print_measurements(struct algo_to_test const * const algos, size_t const num_of_algos,
        size_t const length, perfcounter_t const zeroth, perfcounter_t const * const firsts,
        perfcounter_t const * const seconds) {
#if PRINT_IN_FILE_FRIENDLY_FORMAT
    (void)algos;
    printf("%zd\t", length);
    for (size_t id = 0; id < num_of_algos; id++) {
        perfcounter_t mean = get_mean_of_time(zeroth, firsts[id]);
        perfcounter_t std = get_std_of_time(zeroth, firsts[id], seconds[id]);
        printf("%6lu %5lu\t", mean, std);
    }
#else
    size_t const log = 31 - __builtin_clz(length);
    printf("Length: %zd\n", length);
    for (size_t id = 0; id < num_of_algos; id++) {
        perfcounter_t mean = get_mean_of_time(zeroth, firsts[id]);
        perfcounter_t std = get_std_of_time(zeroth, firsts[id], seconds[id]);
        printf(
            "%-14s: %8lu ±%6lu C \t %5lu C/n \t %5lu C/(n log n) \t %5lu C/n²\n",
            algos[id].name,
            mean,
            std,
            mean / length,
            mean / (length * log),
            mean / (length * length)
        );
    }
#endif
    printf("\n");
}

void test_algos(char const name[], struct algo_to_test const algos[], size_t const num_of_algos,
        size_t const lengths[], size_t const num_of_lengths, triple_buffers const * const buffers,
        struct dpu_arguments const * const args) {
    T *cache = buffers->cache;

    size_t nb_of_bytes_for_moments = num_of_algos * sizeof(perfcounter_t);
    perfcounter_t *first_moments = mem_alloc(nb_of_bytes_for_moments);
    perfcounter_t *second_moments = mem_alloc(nb_of_bytes_for_moments);
    perfcounter_t curr_time, overhead = 0;

    /* Compute overhead. */
    T * const start_empty = cache, * const end_empty = &cache[1];
    for (uint32_t rep = 0; rep < args->n_reps; rep++) {
        curr_time = perfcounter_get();
        empty_sort(start_empty, end_empty);
        curr_time = perfcounter_get() - curr_time;
        overhead += curr_time;
    }
    overhead = get_mean_of_time(args->n_reps, overhead);

    /* Do and time actual repetitions. */
    print_header(name, algos, num_of_algos, args);
    for (size_t li = 0; li < num_of_lengths; li++) {
        memset(first_moments, 0, nb_of_bytes_for_moments);
        memset(second_moments, 0, nb_of_bytes_for_moments);

        size_t const length = lengths[li];
        T * const start = cache, * const end = &cache[length - 1];
        for (uint32_t rep = 0; rep < args->n_reps; rep++) {
            rngs[0] = seed_xs(rep + 0b1011100111010);
            for (size_t id = 0; id < num_of_algos; id++) {
                // generate_uniform_distribution_wram(start, end, args->upper_bound);
                generate_almost_sorted_distribution_wram(start, end, args->upper_bound);
                pivot_rng_state = seed_xs(rep + 0b1011100111010);
                curr_time = perfcounter_get();
                algos[id].algo(start, end);
                curr_time = perfcounter_get() - curr_time - overhead;
                first_moments[id] += curr_time;
                second_moments[id] += curr_time * curr_time;
#if CHECK_SANITY
                T smallest = cache[0];
                for (size_t i = 1; i < length; i++) {
                    if (cache[i] < smallest) {
                        printf("%s: Not sorted!\n", algos[id].name);
                        break;
                    }
                    smallest = cache[i];
                }
#endif
            }
        }

        print_measurements(algos, num_of_algos, length, args->n_reps, first_moments, second_moments);
    }
}
