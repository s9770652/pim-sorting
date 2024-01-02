#ifndef _COMMON_H_
#define _COMMON_H_

// Data Type
#if defined(UINT32)
#define T uint32_t
#define DIV 2  // Shift right to divide by sizeof(T)
#elif defined(UINT64)
#define T uint64_t
#define DIV 3  // Shift right to divide by sizeof(T)
#endif

// Transfer Size Between MRAM and WRAM (<= 2048)
#ifndef BL
#define BL 8
#endif
#define BLOCK_SIZE_LOG2 BL
#define BLOCK_SIZE (1 << BLOCK_SIZE_LOG2)

// Structures used by both the host and the dpu to communicate information
typedef struct {
    uint32_t length;  // number of elements to sort
    uint32_t size;  // size sof all elements to sort (aligned on 8 bytes)
    uint64_t upper_bound;  // maximum value (exclusive) of range to draw from TODO: convert to T (mind the sizeof(dpu_arguments_t))
} dpu_arguments_t;

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define divceil(n, m) (((n)-1) / (m) + 1)
#define roundup(n, m) ((n / m) * m + m)

#endif  // _COMMON_H_