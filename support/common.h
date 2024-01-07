#ifndef _COMMON_H_
#define _COMMON_H_

// Data Type
#if defined(UINT32)
#define T uint32_t
#define DIV 2  // Shift right to divide by sizeof(T).
#elif defined(UINT64)
#define T uint64_t
#define DIV 3  // Shift right to divide by sizeof(T).
#endif

// Structures used by both the host and the dpu to communicate information
typedef struct {
    uint32_t length;  // number of elements to sort
    uint32_t size;  // size sof all elements to sort (aligned on 8 bytes)
    uint64_t upper_bound;  // maximum value (exclusive) of range to draw from TODO: convert to T (mind the sizeof(dpu_arguments_t))
} dpu_arguments_t;

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Computes `n` : `m` rounded up.
#define DIV_CEIL(n, m) (((n)-1) / (m) + 1)
// Rounds `n` up to the next highest multiple of `m` if not already a multiple.
#define ROUND_UP(n, m) ((((n) + (m) - 1) / (m)) * (m))
// Rounds `n` up to the next highest multiple of `m` if not already a multiple.
// `m` must be a power of two.
#define ROUND_UP_POW2(n, m) (((n) + (m) - 1) & -(m))

#endif  // _COMMON_H_