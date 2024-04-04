#ifndef _PARAMS_H_
#define _PARAMS_H_

#include <assert.h>
#include <getopt.h>
#include <string.h>

#include "common.h"

struct Params {
    uint32_t length;  // number of elements to sort
    T upper_bound;  // maximum value (exclusive) of range to draw from
    uint32_t mode;  // benchmark: ID (0=no benchmark)
    uint32_t n_warmup;  // benchmark: how often to repeat measurements
    uint32_t n_reps;  // benchmark: how many repetitions before measuring time
};

static void usage(void) {
    fprintf(stderr,
        "\nUsage: ./host [options]"
        "\n"
        "\nOptions:"
        "\n    -h          help"
        "\n    -n <uint>   input length [default: 512]"
        "\n    -b <uint>   upper bound (exclusive) of range to draw random numbers from (set to 0 to disable) [default: 8]"
        "\n    -w <uint>   # of untimed warm-up iterations [default: 1]"
        "\n    -r <uint>   # of timed repetition iterations [default: 3]"
        "\n    -B <int>    ID of benchmark to run (set to -1 to show list of all benchmark IDs)"
        "\n");
}

static void show_modes(void) {
    fprintf(stderr,
        "\nBenchmark IDs:"
        "\n     0   None/Run normal sorting program (default)"
        "\n     1   Custom ShellSorts"
        "\n"
        "\nSpecify these using -B=<ID> or -B<ID>!"
        "\n");
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.length = 512;
    p.upper_bound = 8;
    p.n_warmup = 1;
    p.n_reps = 3;
    p.mode = 0;

    int opt;
    while ((opt = getopt(argc, argv, "hn:b:w:r:B:")) >= 0) {
        switch(opt) {
        case 'h':
            usage();
            exit(0);
            break;
        case 'n': p.length      = (uint32_t)atof(optarg); break;
        case 'b': p.upper_bound = (T)atof(optarg); break;
        case 'w': p.n_warmup    = (uint32_t)atof(optarg); break;
        case 'r': p.n_reps      = (uint32_t)atof(optarg); break;
        case 'B':
            if (strcmp(optarg, "-1") == 0) {
                show_modes();
                exit(0);
            } else {
                p.mode          = (uint32_t)atof(optarg);
                break;
            }
        default:
            fprintf(stderr, "\nUnrecognized option!\n");
            usage();
            exit(0);
        }
    }
    assert(p.length > 0 && "Input length must be positive!");
    assert(p.n_reps > 0 && "Invalid # of repetition iterations!");

    return p;
}

#endif  // _PARAMS_H_