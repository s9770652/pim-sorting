#include <stdio.h>

#include <defs.h>

#include "timer.h"

void print_time(perfcounter_t *cycles, char *label) {
    if (me() != 0) return;
    perfcounter_t cycles_total = 0, cycles_max = 0;
    for (unsigned t = 0; t < NR_TASKLETS; t++) {
        cycles_total += cycles[t];
        cycles_max = cycles_max < cycles[t] ? cycles[t] : cycles_max;
    }
    printf(
        "time (%s):\t%8.2f ms | %8.2f ms\n",
        label,
        (double)cycles_max / CLOCKS_PER_SEC * 1000,
        (double)cycles_total / CLOCKS_PER_SEC * 1000
    );
}