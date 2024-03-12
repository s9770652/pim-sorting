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
#define QUICK2_LENGTH 768  // base length when using QuickSort2 + InsertionSort
#define QUICK2_INSERTION_BASE 48  // when to switch from QuickSort2 to InsertionSort
#define QUICK3_LENGTH 768
#define QUICK3_INSERTION_BASE 48
#define HEAP_LENGTH 768
#define HEAP_INSERTION_BASE 64

#define BASE_LENGTH HEAP_LENGTH
#define BASE_SIZE (BASE_LENGTH << DIV)
#if (BASE_SIZE > TRIPLE_BUFFER_SIZE)
#error `BASE_LENGTH` does not fit within the three combined WRAM buffers!
#endif


/**
 * @brief A standard implementation of InsertionSort.
 * @attention This algorithm relies on `start[-1]` being a sentinel value,
 * i.e. being at least as small as any value in the array.
 * For this reason, `cache[-1]` is set to `MIN_VALUE`.
 * For QuickSort, the last value of the previous partition takes on that role.
 * 
 * @param start The start of the WRAM array to sort.
 * @param end The (inclusive) end of said array.
**/
static void insertion_sort(T *start, T * const end) {
    T *curr;
    while ((curr = start++) <= end) {  // todo: `++start` is slower‽
        T *prev = curr - 1;
        T const to_sort = *curr;
        while (*prev > to_sort) {
            *curr = *prev;
            curr = prev--;  // always valid due to the sentinel value
        }
        *curr = to_sort;
    }
}

static void swap(T * const a, T * const b) {
    T const temp = *a;
    *a = *b;
    *b = temp;
}

static inline T get_pivot(T const * const start, T const * const end) {
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

static void quick_sort_recursive_2(T * const array, size_t const low, size_t const high) {
    /* Detect base cases. */
    if (high - low <= QUICK2_INSERTION_BASE) {  // false if `high < low` due to wrapping
        insertion_sort(&array[low], &array[high]);
        return;
    } else if (high <= low) return;
    /* Put elements into respective partitions. */
    size_t i = low, j = high;
    T pivot = get_pivot(&array[low], &array[high]);
    do {
        while (array[i] < pivot) i++;
        while (array[j] > pivot) j--;

        if (i <= j)
            swap(&array[i++], &array[j--]);
    } while (i <= j);
    /* Sort left and right partitions. */
    quick_sort_recursive_2(array, low, j);
    quick_sort_recursive_2(array, i, high);
}

static void quick_sort_recursive_3(T * const start, T * const end) {
    /* Detect base cases. */
    if (end - start <= QUICK3_INSERTION_BASE) {  // false if `end < start` due to wrapping
        insertion_sort(start, end);
        return;
    } else if (end <= start) return;
    /* Put elements into respective partitions. */
    T *i = start, *j = end;
    T pivot = get_pivot(start, end);
    do {
        while (*i < pivot) i++;
        while (*j > pivot) j--;

        if (i <= j)
            swap(i++, j--);
    } while (i <= j);
    /* Sort left and right partitions. */
    quick_sort_recursive_3(start, j);
    quick_sort_recursive_3(i, end);
}

static void quick_sort_iterative(T * const start, T * const end) {
    // A “call” stack for holding the values of `left` and `right` is maintained.
    // Since QuickSort works in-place, it is stored right after the end.
    T **stack = (T **)(uintptr_t)end;
    *++stack = start;
    *++stack = end;
    do {
        T *right = *stack--, *left = *stack--;  // Pop from stack.
        /* Detect base cases. */
        if (right - left <= QUICK3_INSERTION_BASE) {  // false if `right < left` due to wrapping
            insertion_sort(left, right);
            continue;
        } else if (right <= left) return;
        /* Put elements into respective partitions. */
        T *i = left, *j = right;
        T pivot = get_pivot(left, right);
        do {
            while (*i < pivot) i++;
            while (*j > pivot) j--;

            if (i <= j)
                swap(i++, j--);
        } while (i <= j);
        /* Put left partition on stack. */
        if (j > left) {
            *++stack = left;
            *++stack = j;
        }
        /* Put right partition on stack. */
        if (right > i) {
            *++stack = i;
            *++stack = right;
        }
    } while (stack != (T **)(uintptr_t)end);
}

static void heapify(T *data, size_t const n, size_t const root) {
    T old_root_value = data[root];
    size_t father = root, son;
    while ((son = father * 2 + 1) < n) {  // left son
        if ((son + 1 < n) && (data[son + 1] > data[son]))  // Check if right son is bigger.
            son++;
        if (data[son] <= old_root_value)  // Stop if both sons are smaller than their father.
            break;
        data[father] = data[son];  // Shift son up.
        father = son;
    }
    data[father] = old_root_value;
}

static void heap_sort(T *data, size_t const n) {
    if (n < 2) return;
    /* Build a heap using Floyd's method. */
    for (size_t r = n / 2; r > 0; r--)
        heapify(data, n, r - 1);
    /* Sort by repeatedly putting the root at the end of the heap. */
    for (size_t i = n - 1; i > HEAP_INSERTION_BASE; i--) {
        swap(&data[0], &data[i]);
        heapify(data, i, 0);
    }
    insertion_sort(data, &data[HEAP_INSERTION_BASE]);
}

static void base_sort(T *array, size_t const length) {
    // insertion_sort(array, &array[length - 1]);
    // quick_sort_recursive_2(array, 0, length - 1);
    quick_sort_recursive_3(array, &array[length - 1]);
    // quick_sort_iterative(array, &array[length - 1]);
    // heap_sort(array, length);
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