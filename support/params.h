#ifndef _PARAMS_H_
#define _PARAMS_H_

#include <assert.h>
#include <getopt.h>

#include "common.h"

struct Params {
    unsigned length;
    T upper_bound;
    unsigned n_warmup;
    unsigned n_reps;
};

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
    p.length = 256 * (1 << 0);
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
        case 'n': p.length      = (unsigned)atof(optarg); break;
        case 'b': p.upper_bound = (T)atof(optarg); break;
        case 'w': p.n_warmup    = (unsigned)atof(optarg); break;
        case 'r': p.n_reps      = (unsigned)atof(optarg); break;
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