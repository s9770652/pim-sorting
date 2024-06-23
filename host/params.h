#ifndef _PARAMS_H_
#define _PARAMS_H_

#include <assert.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "random_distribution.h"

struct Params {
    uint32_t length;  // number of elements to sort
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
        "\n    -d <uint>   Id of the distribution to draw from (set to -1 to show list of all Ids) [default: 3]"
        "\n    -p <uint>   parameter to pass to distribution (set to -1 to show list of all Ids)"
        "\n    -w <uint>   # of untimed warm-up iterations [default: 1]"
        "\n    -r <uint>   # of timed repetition iterations [default: 3]"
        "\n    -B <int>    Id of benchmark to run (set to -1 to show list of all Ids) [default: 0]"
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
        "\n"
    );
}

static void show_param_meanings(void) {
    fprintf(stderr,
        "Parameter Meanings:"
        "\n     Sorted:         Value of the first element (i.e. the smallest) [default: 0]"
        "\n     Reverse:        Value of the last element (i.e. the smallest) [default: 0]"
        "\n     AlmostSorted:   Number of swaps [default: √n]"
        "\n     Uniform:        Upper bound (exclusive) of range to draw from [default: maximum]"
    );
}

static void show_modes(void) {
    fprintf(stderr,
        "Benchmark Ids:"
        "\n     0   None/Run normal sorting program (default)"
        "\n     1   Base sorting algorithms"
        "\n     2   QuickSorts"
        "\n     3   Sorting on a full WRAM cache"
        "\n"
    );
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.length = 512;
    p.dist_type = 3;
    p.dist_param = 0;
    p.n_reps = 3;
    p.mode = 0;

    int opt;
    while ((opt = getopt(argc, argv, "hn:t:p:w:r:B:")) >= 0) {
        switch(opt) {
        case 'h':
            usage();
            exit(0);
            break;
        case 'n': p.length = (uint32_t)atof(optarg); break;
        case 't':
            if (strcmp(optarg, "-1") == 0) {
                show_distributions();
                exit(0);
            } else {
                p.dist_type = (uint32_t)atof(optarg);
                break;
            }
        case 'p':
            if (strcmp(optarg, "-1") == 0) {
                show_param_meanings();
                exit(0);
            } else {
                p.dist_param = (T)atof(optarg);
                break;
            }
        case 'r': p.n_reps = (uint32_t)atof(optarg); break;
        case 'B':
            if (strcmp(optarg, "-1") == 0) {
                show_modes();
                exit(0);
            } else {
                p.mode = (uint32_t)atof(optarg);
                break;
            }
        default:
            fprintf(stderr, "\nUnrecognized option!\n\n");
            usage();
            exit(0);
        }
    }
    assert(p.length > 0 && "Input length must be positive!");
    assert(p.n_reps > 0 && "Invalid # of repetition iterations!");
    assert(p.dist_type < nr_of_dists && "Invalid random distribution type!");

    return p;
}

#endif  // _PARAMS_H_
