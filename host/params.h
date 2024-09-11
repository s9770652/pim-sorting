/**
 * @file
 * @brief Implementation of the command-line interface.
**/

#ifndef _PARAMS_H_
#define _PARAMS_H_

#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "random_distribution.h"

struct Params {
    char *lengths;  // number of elements to sort
    uint32_t mode;  // benchmark: ID (0=no benchmark)
    uint32_t n_reps;  // benchmark: how often to repeat measurements
    uint32_t dist_type;  // distribution to draw from
    T dist_param;  // parameter to pass to distribution
};

static void usage(void) {
    fprintf(stderr,
        "Usage: ./host [options]"
        "\n"
        "\nOptions:"
        "\n    -h          help"
        "\n    -n <uint>   input length [default: 512]"
        "\n    -t <uint>   type of the distribution to draw from (set to -1 to show list of all types) [default: uniform]"
        "\n    -p <uint>   parameter to pass to distribution (set to -1 to show list of all meanings)"
        "\n    -r <uint>   number of timed repetition iterations [default: 3]"
        "\n    -b <int>    Id of the benchmark to run (set to -1 to show list of all Ids) [default: 0]"
        "\n"
    );
}

static void show_distributions(void) {
    fprintf(stderr,
        "Distribution Types:"
        "\n     0   Sorted"
        "\n     1   Reverse"
        "\n     2   AlmostSorted"
        "\n     3   Uniform"
        "\n     4   Zipf"
        "\n     5   Normal"
        "\n"
    );
}

static void show_param_meanings(void) {
    fprintf(stderr,
        "Parameter Meanings:"
        "\n     Sorted:         value of the first element (i.e. the smallest) [default: 0]"
        "\n     Reverse:        value of the last element (i.e. the smallest) [default: 0]"
        "\n     AlmostSorted:   number of swaps [default: √𝘯]"
        "\n     Uniform:        upper bound (exclusive) of range to draw from [default: maximum]"
        "\n     Zipf:           /"
        "\n     Normal:         standard deviation [default: 𝘯/8]"
        "\n"
        "\nNon-zero default values internally equal zero as well."
        "\n"
    );
}

static void show_modes(void) {
    fprintf(stderr,
        "Benchmark Ids:"
        "\n     0   None/Run normal sorting program (default)"
        "\n     1   Base sorting algorithms (WRAM)"
        "\n     2   QuickSorts (WRAM)"
        "\n     3   MergeSorts (WRAM)"
        "\n     4   HeapSorts (WRAM)"
        "\n     5   MergeSort (MRAM, half-space, straight reader)"
        "\n     6   MergeSort (MRAM, half-space, custom reader)"
        "\n     7   MergeSort (MRAM, full-space, straight reader)"
        "\n     8   MergeSort (parallel)"
        "\n"
    );
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.lengths = "512";
    p.dist_type = 3;
    p.dist_param = 0;
    p.n_reps = 1;
    p.mode = 0;

    int opt;
    while ((opt = getopt(argc, argv, "hn:t:p:w:r:b:")) >= 0) {
        double value = atof(optarg);
        switch(opt) {
        case 'h':
            usage();
            exit(0);
            break;
        case 'n':
            p.lengths = optarg;
            break;
        case 't':
            if (strcmp(optarg, "-1") == 0) {
                show_distributions();
                exit(0);
            } else {
                assert(value < nr_of_dists && "Invalid random distribution type!");
                assert(value >= 0 && "Invalid random distribution type!");
                p.dist_type = value;
                break;
            }
        case 'p':
            if (strcmp(optarg, "-1") == 0) {
                show_param_meanings();
                exit(0);
            } else {
                assert(value >= 0 && "Distribution parameter must be non-negative!");
                p.dist_param = value;
                break;
            }
        case 'r':
            assert(value > 0 && "Number of iterations must be positive!");
            p.n_reps = value;
            break;
        case 'b':
            if (strcmp(optarg, "-1") == 0) {
                show_modes();
                exit(0);
            } else {
                assert(value >= 0 && "Invalid benchmark Id!");  // Big values are caught in app.c.
                p.mode = value;
                break;
            }
        default:
            fprintf(stderr, "\nUnrecognized option!\n\n");
            usage();
            exit(0);
        }
    }
    return p;
}

#endif  // _PARAMS_H_
