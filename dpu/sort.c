#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <defs.h>
#include <handshake.h>
#include <mram.h>
#include <seqread.h>

#include "../support/common.h"
#include "mram_loop.h"
#include "sort.h"

#define INSERTION_LENGTH 48  // base length when using just InsertionSort
#define QUICK1_LENGTH 128  // base length when using QuickSort1 + InsertionSort
#define QUICK1_INSERTION_BASE 32  // when to switch from QuickSort1 to InsertionSort
#define QUICK2_LENGTH 768
#define QUICK2_INSERTION_BASE 48
#define QUICK3_LENGTH 768
#define QUICK3_INSERTION_BASE 48

#define BASE_LENGTH QUICK3_LENGTH
#define BASE_SIZE (BASE_LENGTH << DIV)
#if (BASE_SIZE > TRIPLE_BUFFER_SIZE)
#error `BASE_LENGTH` does not fit within the three combined WRAM buffers!
#endif

typedef int32_t ssize_t;


/**
 * @brief A standard implementation of InsertionSort.
 * @attention This algorithm relies on `array[-1]` being a sentinel value,
 * i.e. being at least as small as any value in the array.
 * For this reason, `cache[-1]` is set to `MIN_VALUE`.
 * For QuickSort, the last value of the previous partition takes on that role.
 * 
 * @param array The WRAM array to sort.
 * @param length The length of the array.
**/
static void insertion_sort(T *array, size_t const length) {
    for (size_t i = 1; i < length; i++) {
        T *curr = &array[i];
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

static void swap(T *a, T *b) {
    T temp = *a;
    *a = *b;
    *b = temp;
}

static inline T get_pivot(T *start, T *end) {
    (void)start;  // Gets optimised away …
    (void)end;  // … but suppresses potential warnings about unused functions.
    /* Always the leftmost element. */
    // return *start;
    /* The mean of the leftmost and the rightmost element. */
    return (*start + *end) / 2;
    /* The median of the leftmost, middle and rightmost element. */
    // T *middle = (T *)(((uintptr_t)start + (uintptr_t)end) / 2 & ~(sizeof(T)-1));
    // if ((*start > *middle) ^ (*start > *end))
    //     return *start;
    // else if ((*middle < *start) ^ (*middle < *end))
    //     return *middle;
    // else
    //     return *end;
}


static ssize_t my_partition(T *array, ssize_t const low, ssize_t const high) {
    T pivot = get_pivot(&array[low], &array[high]);
    ssize_t i = low, j = high;
    while (i < j) {
        while (array[i] <= pivot && i <= high - 1) {
            i++;
        }
        while (array[j] > pivot && j >= low + 1) {
            j--;
        }
        if (i < j) {
            swap(&array[i], &array[j]);
        }
    }
    swap(&array[low], &array[j]);
    return j;
}

static void quick_sort_recursive_1(T *array, ssize_t const low, ssize_t const high) {
    if (low >= high) return;
    if (high - low <= QUICK1_INSERTION_BASE) {
        insertion_sort(&array[low], high - low + 1);
        return;
    }
    ssize_t index = my_partition(array, low, high);

    quick_sort_recursive_1(array, low, index - 1);
    quick_sort_recursive_1(array, index + 1, high);
}

static void quick_sort_recursive_2(T *array, ssize_t const low, ssize_t const high) {
    if (high - low <= QUICK2_INSERTION_BASE) {  // false if `end < start` due to wrapping
        insertion_sort(&array[low], high - low + 1);
        return;
    }
    /* Put elements into respective partitions. */
    ssize_t i = low, j = high;
    T pivot = get_pivot(&array[low], &array[high]);
    while (i <= j) {
        while (array[i] < pivot) i++;
        while (array[j] > pivot) j--;

        if (i <= j) {
            swap(&array[i], &array[j]);
            i++;
            j--;
        }
    }
    /* Sort left and right partitions. */
    quick_sort_recursive_2(array, low, j);
    quick_sort_recursive_2(array, i, high);
}

static void quick_sort_recursive_3(T *start, T *end) {
    if (end - start <= QUICK3_INSERTION_BASE) {  // false if `end < start` due to wrapping
        insertion_sort(start, end - start + 1);
        return;
    }
    /* Put elements into respective partitions. */
    T *i = start, *j = end;
    T pivot = get_pivot(start, end);
    while (i <= j) {
        while (*i < pivot) i++;
        while (*j > pivot) j--;

        if (i <= j)
            swap(i++, j--);
    }
    /* Sort left and right partitions. */
    quick_sort_recursive_3(start, j);
    quick_sort_recursive_3(i, end);
}

static void quick_sort_iterative(T *start, T *end) {
    T **stack = (T **)(uintptr_t)(end + 2);
    T **ptr = stack;
    *ptr = start; ptr++;
    *ptr = end; ptr++;
    do {
        T *right = *--ptr, *left = *--ptr;
        if (right - left <= QUICK3_INSERTION_BASE) {
            insertion_sort(left, right - left + 1);
            continue;
        }
        /* Put elements into respective partitions. */
        T *i = left, *j = right;
        T pivot = get_pivot(left, right);
        while (i <= j) {
            while (*i < pivot) i++;
            while (*j > pivot) j--;

            if (i <= j)
                swap(i++, j--);
        }
        /* Put left partition on stack. */
        if (j > left) {
            *ptr = left; ptr++;
            *ptr = j; ptr++;
        }
        /* Put right partition on stack. */
        if (right > i) {
            *ptr = i; ptr++;
            *ptr = right; ptr++;
        }
    } while (ptr != stack);
}

static void base_sort(T *array, size_t const length) {
    // insertion_sort(array, length);
    // quick_sort_recursive_1(array, 0, length - 1);
    // quick_sort_recursive_2(array, 0, length - 1);
    quick_sort_recursive_3(array, &array[length-1]);
    // quick_sort_iterative(array, &array[length-1]);
    }

// Inlining actually worsens performance.
static __noinline void deplete(T __mram_ptr *input, T __mram_ptr *output, T *cache, T *ptr,
        size_t i, T __mram_ptr const *end) {
    // Transfer cache to MRAM.
#ifdef UINT32
    if (i & 1) {  // Is there need for alignment?
        cache[i++] = *ptr++;  // Possible since `ptr` must have at least one element.
        if (++input == end) {
            mram_write(cache, output, i << DIV);
            return;
        };
    }
#endif
    mram_write(cache, output, i << DIV);
    output += i;

    // Transfer from MRAM to MRAM.
    do {
        // Thanks to the dummy values, even for numbers smaller than 8 bytes,
        // there is no need to round the size up.
        size_t rem_size = (input + BLOCK_LENGTH > end) ? (size_t)end - (size_t)input : BLOCK_SIZE;
        mram_read(input, cache, rem_size);
        mram_write(cache, output, rem_size);
        input += BLOCK_LENGTH;  // Value may be wrong for the last transfer …
        output += BLOCK_LENGTH;  // … after which it is not needed anymore, however.
    } while (input < end);
}

bool merge(T __mram_ptr *input, T __mram_ptr *output, triple_buffers *buffers, const mram_range ranges[NR_TASKLETS]) {
    T *cache = buffers->cache;
    seqreader_t sr[2];
    bool flipped = false;  // Whether `input` or `output` contain the sorted elements.
    size_t initial_run_length = BASE_LENGTH;
    mram_range range = { ranges[me()].start, ranges[me()].end };
    // `brother_mask` is used to determine the brother node in the tournament tree.
    for (size_t brother_mask = 1; true; brother_mask <<= 1) {
        for (size_t run = initial_run_length; run < range.end - range.start; run <<= 1) {
            for (size_t j = range.start; j < range.end; j += run << 1) {
                // If it is just one run, there is nothing to merge.
                // Just move its elements immediately from `input` to `output`.
                if (j + run >= range.end) {
                    size_t i, curr_length, curr_size;
                    mram_range deplete_range = { j, range.end };
                    LOOP_ON_MRAM(i, curr_length, curr_size, deplete_range) {
                        mram_read(&input[i], cache, curr_size);
                        mram_write(cache, &output[i], curr_size);
                    }
                    break;
                }
                // Otherwise, merging is needed.
                T *ptr[2] = {
                    seqread_init(buffers->seq_1, &input[j], &sr[0]),
                    seqread_init(buffers->seq_2, &input[j + run], &sr[1])
                };
                const T __mram_ptr *ends[2] = {
                    &input[j + run],
                    &input[(j + (run << 1) <= range.end) ? j + (run << 1) : range.end ]
                };
                size_t elems_left[2] = { run, ends[1] - ends[0] };
                size_t i = 0, written = 0;
                while (true) {
                    #define INSERT(ACT, PAS)                                                                            \
                    cache[i++] = *ptr[ACT];                                                                             \
                    ptr[ACT] = seqread_get(ptr[ACT], sizeof(T), &sr[ACT]);                                              \
                    if (--elems_left[ACT] == 0) {                                                                       \
                        deplete(seqread_tell(ptr[PAS], &sr[PAS]), &output[j + written], cache, ptr[PAS], i, ends[PAS]); \
                        break;                                                                                          \
                    }
                    if (*ptr[0] < *ptr[1]) {
                        INSERT(0, 1);
                    } else {
                        INSERT(1, 0);
                    }
                    // If the cache is full, write its content to `output`.
                    if (i == BLOCK_LENGTH) {
                        mram_write(cache, &output[j + written], BLOCK_SIZE);
                        i = 0;
                        written += BLOCK_LENGTH;
                    }
                }
            }
            // Flip `input` and `output`. Thus, `input` will always contain the biggest runs.
            T __mram_ptr *tmp = input;
            input = output;
            output = tmp;
            flipped = !flipped;
        }
        if ((me() & brother_mask)) {
            handshake_notify();
            break;
        } else {
            if (brother_mask == NR_TASKLETS) break;
            handshake_wait_for(me() | brother_mask);
        }
        initial_run_length = range.end - range.start;
        range.end = ranges[ me() | ((brother_mask << 1) - 1) ].end;
    }
    return flipped;
}

bool sort(T __mram_ptr *input, T __mram_ptr *output, triple_buffers *buffers, const mram_range ranges[NR_TASKLETS]) {
    T *cache = buffers->cache;
    /* Insertion sort by each tasklet. */
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM_BL(i, curr_length, curr_size, ranges[me()], BASE_LENGTH) {
#if (BASE_SIZE <= MAX_TRANSFER_SIZE)
        mram_read(&input[i], cache, curr_size);
        base_sort(cache, curr_length);
        mram_write(cache, &input[i], curr_size);
#else
        mram_read_triple(&input[i], cache, curr_size);
        base_sort(cache, curr_length);
        mram_write_triple(cache, &input[i], curr_size);
#endif
    }
    /* Merge by tasklets. */
    return merge(input, output, buffers, ranges);
}