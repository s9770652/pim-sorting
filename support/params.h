#ifndef _PARAMS_H_
#define _PARAMS_H_

#include "common.h"

typedef struct Params {
    unsigned int length;
    T upper_bound;
    int n_warmup;
    int n_reps;
} Params;

#endif