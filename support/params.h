#ifndef _PARAMS_H_
#define _PARAMS_H_

#include <getopt.h>

#include "common.h"

typedef struct Params {
    unsigned int length;
    T upper_bound;
    int n_warmup;
    int n_reps;
} Params;

static void usage() {
    fprintf(stderr,
        "\nUsage:  ./program [options]"
        "\n"
        "\nOptions:"
        "\n    -h        help"
        "\n    -w <W>    # of untimed warmup iterations (default=1)"
        "\n    -r <R>    # of timed repetition iterations (default=3)"
        "\n    -n <N>    input length (default=4096 elements)"
        "\n    -b <B>    upper bound (exclusive) of range to draw random numbers from"
        "\n");
}

struct Params input_params(int argc, char **argv) {
    struct Params p;
    p.length = 4096;
    p.upper_bound = 8;
    p.n_warmup = 1;
    p.n_reps = 3;

    int opt;
    while ((opt = getopt(argc, argv, "hn:w:r:")) >= 0) {
        switch(opt) {
        case 'h':
            usage();
            exit(0);
            break;
        case 'n': p.length      = atoi(optarg); break;
        case 'b': p.upper_bound = atoi(optarg); break;
        case 'w': p.n_warmup    = atoi(optarg); break;
        case 'r': p.n_reps      = atoi(optarg); break;
        default:
            fprintf(stderr, "\nUnrecognized option!\n");
            usage();
            exit(0);
        }
    }
    assert(NR_DPUS > 0 && "Invalid # of dpus!");

    return p;
}

#endif  // _PARAMS_H_