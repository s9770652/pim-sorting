/**
 * @file
 * @brief Data sent to and from the DPU.
**/

#ifndef _COMMUNICATION_H_
#define _COMMUNICATION_H_

#include <stdint.h>

#include "common.h"

/* Defining some elements missing on the host. */

#ifndef DMA_ALIGNMENT  // Define missing DMA constants for the host.

#define DMA_ALIGNMENT (8)
#define ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define ALIGN(x, a) ALIGN_MASK((x), (a)-1)
#define DMA_ALIGNED(x) ALIGN(x, DMA_ALIGNMENT)

#else  // Check if the constants have changed.

#if (DMA_ALIGNMENT != 8)
#error `DMA_ALIGNMENT` is not 8 anymore! Change the makeshift definitions for the host accordingly!
#endif

#endif

#ifndef __mram_ptr  // Ignore this identifier on the host.
#define __mram_ptr
#endif

/* Defining elements common to both the host and the DPUs. */

#if CHECK_SANITY  // Checkers print so the available MRAM is smaller.
/// @brief The maximum number of elements loaded into MRAM.
/// Their size must be divisible by `DMA_ALIGNMENT`.
#define LOAD_INTO_MRAM ((1024 * 1024 * 31) >> DIV)
#else
/// @brief The maximum number of elements loaded into MRAM.
/// Their size must be divisible by `DMA_ALIGNMENT`.
/// @note The total size is slightly below 32 MiB to ensure that a DMA of a sequential reader
/// near the end does not access nonexistent data.
#define LOAD_INTO_MRAM ((1024 * 1024 * 32 - SEQREAD_CACHE_SIZE) >> DIV)
#endif

#if ((LOAD_INTO_MRAM << DIV) != DMA_ALIGNED(LOAD_INTO_MRAM << DIV))
#error The size of elements to load into MRAM must be divisible by `DMA_ALIGNMENT`.
#endif

/// @brief Every WRAM sorting function must adher to this pattern.
typedef void sort_algo_wram(T *, T *);

/// @brief Every MRAM sorting function must adher to this pattern.
typedef void sort_algo_mram(T __mram_ptr *, T __mram_ptr *);

/// @brief A general sorting function.
union sort_algo {
    sort_algo_wram *wram;
    sort_algo_mram *mram;
};

/// @brief Information sent from the host to the DPU.
struct dpu_arguments {
    /// @brief How many repetitions are performed.
    uint32_t reps;
    /// @brief The number of elements to sort.
    uint32_t length;
    /// @brief The distance between the input data of the different tests.
    /// @note This value times `sizeof(T)` is aligned for DMAs.
    uint32_t offset;
    /// @brief The number of elements to sort by one tasklet in the sequential phase.
    uint32_t part_length;
    /// @brief The seed used by all tasklets, which then offset with their own Id.
    uint32_t basic_seed;
    /// @brief The index of the sorting algorithm to run.
    uint32_t algo_index;
};

/// @brief The data type holding the performance counter count.
/// This is needed since `perfcounter_t` is only available on a DPU.
typedef uint64_t time;

/// @brief Information sent from the DPU to the host.
struct dpu_results {
    /// @brief The sum of the measured times.
    time firsts;
    /// @brief The sum of the squares of the measured times.
    time seconds;
};

/// @brief A sorting algorithm and its name.
struct algo_data {
    /// @brief The name of the algorithm to print in the console.
    char name[16];
    /// @brief A pointer to a sorting function of type `base_sort_algo`.
    /// Since pointers have different sizes on the host and the DPU,
    /// a conversion to a normal integer is needed.
    union sort_algo fct;
};

/// @brief Holds a sorting algorithm and its name.
/// Has the same size on 32-bit systems (DPU) and 64-bit ones (probably the host).
union algo_to_test {
    struct algo_data data;
    char padding[24];
};

/// @brief The experimentally determined overhead of calling a sorting function.
#define CALL_OVERHEAD (144)

#endif  // _COMMUNICATION_H_
