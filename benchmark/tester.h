#include "buffers.h"
#include "common.h"

/// @brief Every WRAM sorting functions must adher to this pattern.
typedef void base_sort_algo(T *, T *);

/**
 * @brief Holds both a pointer to and the name of a function.
 * Useful for creating lists of functions to test.
**/
struct algo_to_test {
    /// @brief A pointer to a WRAM sorting function.
    base_sort_algo *algo;
    /// @brief The name of a WRAM sorting function.
    char name[14];
};

/**
 * @brief Runs a set of algorithms and prints their runtimes.
 * 
 * @param name The name of the test to be shown in the console.
 * @param algos A list of algorithms and their names.
 * @param num_of_algos The length of the algorithm list.
 * @param lengths A list of input lengths on which to run the algorithms.
 * @param num_of_lengths The length of the length list.
 * @param buffers A struct containing a WRAM cache.
 * @param args The arguments with which the program was started,
 * including the number of repetitions and the upper bound for random numbers.
**/
void test_algos(char const name[], struct algo_to_test const algos[], size_t num_of_algos,
        size_t const lengths[], size_t num_of_lengths, triple_buffers const *buffers,
        struct dpu_arguments const *args);
