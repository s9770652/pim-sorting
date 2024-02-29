/**
 * @file
 * @brief Displaying the time measured by multiple tasklets.
**/

#ifndef _TIMER_H_
#define _TIMER_H_

#include <perfcounter.h>

/**
 * @brief Prints the maximum duration and total time of an array of cycle counts.
 * 
 * `perfcounter_config` must have been called with `COUNT_CYCLES` beforehand.
 * The current cycle counts in `cycles` should be gotten using `perfcounter_get`.
 * 
 * @param cycles The array of cycle counts of length `NR_TASKLETS` to use for calculation.
 * @param label The label to print in front of the duration and total time.
**/
void print_time(perfcounter_t *cycles, char *label);

#endif