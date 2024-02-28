#include <stddef.h>
#include <stdio.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mutex.h>
#include <perfcounter.h>

#include "../support/common.h"
#include "checkers.h"
#include "sort.h"
#include "random.h"

// maximum number of elements loaded into MRAM (size must be divisible by 8)
#define LOAD_INTO_MRAM ((1024 * 1024 * 25) >> DIV)

__host struct dpu_arguments DPU_INPUT_ARGUMENTS;
T __mram_noinit input[LOAD_INTO_MRAM];  // array of random numbers
T __mram_noinit output[LOAD_INTO_MRAM];
mram_range ranges[NR_TASKLETS];
static struct xorshift rngs[NR_TASKLETS];  // Contains the seed for each tasklet.
bool flipped;

BARRIER_INIT(omni_barrier, NR_TASKLETS);

#if PERF
perfcounter_t cycles[NR_TASKLETS];  // Used to measure the time for each tasklet.
#endif

#if CHECK_SANITY
bool same_elems, sorted;
MUTEX_INIT(sorted_mutex);
#endif


inline size_t align(size_t to_align) {
    return ROUND_UP_POW2(to_align << DIV, 8) >> DIV;
}

/**
 * @brief Calculates the sum of a given MRAM array
 * and also prints its content if the length is at most 2048.
 * For this reason, this function is currently sequential.
 * @param array The MRAM array whose sum is to be calculated.
 * @param cache A cache in WRAM.
 * @param counts Array of occurrences of each array element smaller than 8.
 * Should be initialised to all zeroes.
 * @param length The length of the MRAM array.
 * @param label The text to be shown before the array is printed.
 * @return The sum of all elements in `array`.
 */
uint64_t get_sum(T __mram_ptr *array, T *cache, size_t counts[8], size_t const length, char *label) {
    if (me() != 0) return 0;
    size_t max_length_to_print = 2048;
    if (length <= max_length_to_print) {
        printf("%s", label);
    }
    uint64_t sum = 0;
    size_t i, curr_length, curr_size;
    mram_range sum_range = { 0, length };
    LOOP_ON_MRAM(i, curr_length, curr_size, sum_range) {
        mram_read(&array[i], cache, curr_size);
        if (length <= max_length_to_print) {
            print_array(cache, curr_length);
        }
        for (size_t x = 0; x < curr_length; x++) {
            sum += cache[x];
            if (cache[x] < 8)
                counts[cache[x]]++;
        }
    }
    if (length <= max_length_to_print) {
        printf("\n");
    }
    return sum;
}

int main() {
    if (me() == 0) {
        mem_reset();
#if CHECK_SANITY
        sorted = true;
#endif
#if PERF
        perfcounter_config(COUNT_CYCLES, true);
#endif
        printf("input length: %d\n", DPU_INPUT_ARGUMENTS.length);
        printf("diff to max length: %d\n", LOAD_INTO_MRAM - DPU_INPUT_ARGUMENTS.length);
        printf("BLOCK_SIZE: %d\n", BLOCK_SIZE);
        // printf("HEAPPOINTER: %p\n", DPU_MRAM_HEAP_POINTER);
        // printf("T in MRAM: %d\n", 2 * LOAD_INTO_MRAM);
        // printf("free in MRAM: %d\n", 1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER);
        // printf("more T in MRAM: %d\n", (1024*1024*64 - (uint32_t)DPU_MRAM_HEAP_POINTER) >> DIV);
    }
    barrier_wait(&omni_barrier);

    /* Compute addresses and boundaries of arrays in WRAM and MRAM. */
    // input length per DPU in number of elements
    const size_t length = DPU_INPUT_ARGUMENTS.length;
    // maximum number of elements in the subarray filled by each tasklet
    const size_t part_length = align(DIV_CEIL(length, NR_TASKLETS));
    // start of the tasklet's subarray
    const size_t part_start = me() * part_length;
#ifdef UINT32
    // end of the tasklet's subarray
    const size_t part_end = (me() == NR_TASKLETS - 1) ? ROUND_UP_POW2(length, 2) : part_start + part_length;
#else
    // end of the tasklet's subarray
    const size_t part_end = (me() == NR_TASKLETS - 1) ? length : part_start + part_length;
#endif
    // mram_range range = { part_start, part_end };
    ranges[me()].start = part_start;
    ranges[me()].end = part_end;

    /* Write random numbers onto the MRAM. */
    rngs[me()] = seed_xs(me() + 0b100111010);  // The binary number is arbitrarily chosen to introduce some 1s to improve the seed.
    // Initialize a local cache to store one MRAM block.
    T *cache = mem_alloc(BLOCK_SIZE);
#if PERF
    cycles[me()] = perfcounter_get();
#endif
    size_t i, curr_length, curr_size;
    LOOP_ON_MRAM(i, curr_length, curr_size, ranges[me()]) {
        for (size_t j = 0; j < curr_length; j++) {
            // cache[j] = rr((T)DPU_INPUT_ARGUMENTS.upper_bound, &rngs[me()]);
            cache[j] = (gen_xs(&rngs[me()]) & 7);
            // cache[j] = i + j;
            // cache[j] = length - (i + j) - 1;
            // cache[j] = 0;
        }
        mram_write(cache, &input[i], curr_size);
    }
#ifdef UINT32
    // Add a dummy variable such that the last run has a length disible by 8.
    // This way, depleting (cf. `sort.c`) need less meddling with unaligned addresses.
    if (me() == NR_TASKLETS - 1 && length & 1) {
        input[length] = UINT32_MAX;
    }
#endif
#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
#endif
    barrier_wait(&omni_barrier);
#if PERF
    if (me() == 0) {
        get_time(cycles, "MEMORY");
    }
    barrier_wait(&omni_barrier);
#endif

#if CHECK_SANITY
    size_t counts_1[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t sum_1 = get_sum(input, cache, counts_1, length, "Before sorting:\n");
    barrier_wait(&omni_barrier);
#endif

    /* Sort the elements. */
#if PERF
    cycles[me()] = perfcounter_get();
#endif
    bool flipped_own = sort(input, output, cache, ranges);
#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
#endif
    if (me() == 0) flipped = flipped_own;
    barrier_wait(&omni_barrier);
    T __mram_ptr *within = (flipped) ? output : input;
#if PERF
    if (me() == 0) {
        get_time(cycles, "SORT");
    }
    barrier_wait(&omni_barrier);
#endif

#if CHECK_SANITY
    /* Check if the numbers stayed the same. */
    size_t counts_2[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint64_t sum_2 = get_sum(within, cache, counts_2, length, "After sorting:\n");
    if (me() == 0) {
        same_elems = sum_1 == sum_2;
        for (size_t c = 0; c < 8; c++) {
            same_elems = same_elems && counts_1[c] == counts_2[c];
        }
        if (!same_elems) {
            printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements have changed.\n");
            printf("\nSums: %lu ↔ %lu\nCounts: ", sum_1, sum_2);
            for (size_t c = 0; c < 8; c++) {
                printf("%d: %zu ↔ %zu   ", c, counts_1[c], counts_2[c]);
            }
            printf("\n");
        }
    }
    barrier_wait(&omni_barrier);

    /* Check if numbers were correctly sorted. */
#if PERF
    cycles[me()] = perfcounter_get();
#endif
    T prev = (me() == 0) ? 0 : within[ranges[me()].start-1];
    LOOP_ON_MRAM(i, curr_length, curr_size, ranges[me()]) {
        if (!sorted) break;
        mram_read(&within[i], cache, curr_size);
        if ((prev > cache[0]) || (!is_sorted(cache, curr_length))) {
            mutex_lock(sorted_mutex);
            sorted = false;
            mutex_unlock(sorted_mutex);
            break;
        }
        prev = cache[BLOCK_LENGTH-1];
    }
#if PERF
    cycles[me()] = perfcounter_get() - cycles[me()];
#endif
    barrier_wait(&omni_barrier);
#if PERF
    if (me() == 0) {
        get_time(cycles, "CHECK");
    }
#endif
    if (me() == 0) {
        if (!sorted) {
            printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Elements are not sorted.\n");
        } else if (same_elems) {
            printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Elements are sorted.\n");
        }
    }
#endif
    return 0;
}