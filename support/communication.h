/**
 * @file
 * @brief Data sent to and from the DPU.
**/

#ifndef _COMMUNICATION_H_
#define _COMMUNICATION_H_

#include <stdint.h>

#include "common.h"

/// @brief The maximum number of elements loaded into MRAM. Their size must be divisible by 8.
#define LOAD_INTO_MRAM ((1024 * 1024 * 25) >> DIV)

/// @brief Every WRAM sorting functions must adher to this pattern.
typedef void base_sort_algo(T *, T *);

/// @brief Information sent from the host to the DPU.
struct dpu_arguments {
    /// @brief The number of elements to sort.
    uint32_t length;
    /// @brief The seed used by all tasklets, which then offset with their own Id.
    uint32_t basic_seed;
    /// @brief The index of the sorting algorithm to run.
    uint32_t algo_index;
};

/// @brief The data type holding the performance counter count.
/// This is needed since `perfcounter_t` is only available on a DPU.
typedef uint64_t time;

/// @brief A sorting algorithm and its name.
struct algo_data {
    /// @brief The name of the algorithm to print in the console.
    char name[16];
    /// @brief A pointer to a sorting function of type `base_sort_algo`.
    /// Since pointers have different sizes on the host and the DPU,
    /// a conversion to a normal integer is needed.
    base_sort_algo *fct;
};

/// @brief Holds a sorting algorithm and its name.
/// Has the same size on 32-bit systems (DPU) and 64-bit ones (probably the host).
union algo_to_test {
    struct algo_data data;
    char padding[24];
};

/// @brief The experimentally determined overhead of calling a sorting function.
#define CALL_OVERHEAD (223)

#endif  // _COMMUNICATION_H_
