#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>

// Data Type
#if defined(UINT32)
typedef uint32_t T;
#define DIV (2)  // Shift right to divide by `sizeof(T)`.
#define T_MIN (0)
#define T_MAX (UINT32_MAX)
#define TYPE_NAME "UINT32"
#define T_QUALIFIER "u"
#elif defined(UINT64)
typedef uint64_t T;
#define DIV (3)  // Shift right to divide by `sizeof(T)`.
#define T_MIN (0)
#define T_MAX (UINT64_MAX)
#define TYPE_NAME "UINT64"
#define T_QUALIFIER "lu"
#endif

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/// @brief Computes `n` ÷ `m` rounded up.
#define DIV_CEIL(n, m) (((n) - 1) / (m) + 1)

/**
 * @brief Swaps the content of two addresses.
 * @note If any address involved is an MRAM address,
 * a solution with `mram_write/copy` should be employed.
 * 
 * @param a First address.
 * @param b Second address.
**/
static __attribute__((__always_inline__)) inline void swap(T * const a, T * const b) {
    T const temp = *a;
    *a = *b;
    *b = temp;
}

#endif  // _COMMON_H_
