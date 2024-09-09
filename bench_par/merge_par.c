#include <stdlib.h>
#include <string.h>

#include <barrier.h>
#include <defs.h>
#include <handshake.h>
#include <memmram_utils.h>
#include <perfcounter.h>

#include "checkers.h"
#include "communication.h"
#include "mram_merging.h"
#include "mram_sorts.h"
#include "random_distribution.h"
#include "reader.h"

struct dpu_arguments __host host_to_dpu;
struct dpu_results __host dpu_to_host;
T __mram_noinit_keep input[LOAD_INTO_MRAM];  // set by the host
T __mram_noinit_keep output[LOAD_INTO_MRAM];

triple_buffers buffers[NR_TASKLETS];
struct xorshift input_rngs[NR_TASKLETS];  // RNG state for generating the input (in debug mode)
struct xorshift_offset pivot_rngs[NR_TASKLETS];  // RNG state for choosing the pivot
array_stats stats_before, stats_after;

BARRIER_INIT(omni_barrier, NR_TASKLETS);

seqreader_t sr[NR_TASKLETS][2];  // sequential readers used to read runs
bool flipped[NR_TASKLETS];  // Whether a write-back from the auxiliary array is (not) needed.
mram_range from[NR_TASKLETS][2];
mram_range to[NR_TASKLETS];
size_t borders[NR_TASKLETS];

size_t binary_search(T const to_find, T __mram_ptr *array, size_t start, size_t end) {
    if (end <= start) return start;
    size_t left = start, right = end + 1;
    while (left < right) {
        size_t const middle = (left + right) / 2;  // No overflow due to the small MRAM.
        if (to_find <= array[middle])
            right = middle;
        else
            left = middle + 1;
    }
    return right;
}

static void merge_par(size_t l, size_t r) {
    seqreader_buffer_t wram[2] = { buffers[me()].seq_1, buffers[me()].seq_2 };
    T __mram_ptr *in = (flipped[me()]) ? output : input;
    T __mram_ptr *out = (flipped[me()]) ? input : output;

    // print_array(out, buffers[me()].cache, host_to_dpu.length, "Before sorted");

    size_t mine = me(), yours = (me() == 0) ? me() + 1 : me() - 1;
    from[mine][0].start = l, from[mine][0].end = r;
    if (me() == 0) {
        handshake_wait_for(yours);
        mram_range runs[2];  // 0: shorter; 1: longer
        if ((from[mine][0].end - from[mine][0].start) <= (from[yours][0].end - from[yours][0].start)) {
            memcpy(&runs[0], &from[mine][0], sizeof(mram_range));
            memcpy(&runs[1], &from[yours][0], sizeof(mram_range));
        } else {
            memcpy(&runs[0], &from[yours][0], sizeof(mram_range));
            memcpy(&runs[1], &from[mine][0], sizeof(mram_range));
        }
        // printf("[%u] l %zu  r %zu\n", 0, from[0][0].start, from[0][0].end);
        // printf("[%u] l %zu  r %zu\n", 1, from[1][0].start, from[1][0].end);

        size_t pivot = (runs[1].start + runs[1].end) / 2;
        size_t cut_at = binary_search(in[pivot], in, runs[0].start, runs[0].end);

        size_t border = (pivot - runs[1].start) + (cut_at - runs[0].start);
        out[border] = in[pivot];

        from[yours][0].start = cut_at;
        from[yours][0].end = runs[0].end;
        from[yours][1].start = pivot + 1;
        from[yours][1].end = runs[1].end;
        borders[yours] = border + 1;
        handshake_notify();
        // printf("borders[yours] %zu\n", borders[yours]);
        // printf("runs[1].end %p (eig. %p)\n", &in[runs[1].end], &in[from[yours][0].end]);

        T __mram_ptr *starts[2] = { &in[runs[0].start], &in[runs[1].start] };
        T __mram_ptr *ends[2] = { &in[cut_at - 1], &in[pivot - 1] };

        // printf(
        //     "[%u]\n"
        //     "pivot %u (%u); cut_at %u (%u)\n"
        //     "starts: %p (%u) %p (%u)\n"
        //     "ends: %p (%u) %p (%u)\n"
        //     "Gesamtlänge: %u (%u + %u)\n",
        //     me(),
        //     pivot, in[pivot], cut_at, in[cut_at],
        //     starts[0], *starts[0], starts[1], *starts[1],
        //     ends[0], *ends[0], ends[1], *ends[1],
        //     (ends[0] - starts[0] + 1) + (ends[1] - starts[1] + 1),
        //     ends[0] - starts[0] + 1, ends[1] - starts[1] + 1
        // );

        if ((intptr_t)starts[0] > (intptr_t)ends[0]) {  // The shorter run may be empty.
            flush_run_(starts[1], ends[1], out);
        } else {
            T *ptr[2] = {
                sr_init(buffers[me()].seq_1, starts[0], &sr[me()][0]),
                sr_init(buffers[me()].seq_2, starts[1], &sr[me()][1]),
            };
            merge_mram(ptr, ends, out, wram);
        }

        // ends[0] = &in[runs[0]->end];
        // ends[1] = &in[runs[1]->end];
        // ptr[0] = sr_init(buffers[me()].seq_1, &in[cut_at], &sr[me()][0]);
        // ptr[1] = sr_init(buffers[me()].seq_2, &in[pivot + 1], &sr[me()][1]);

        // printf("starts: %p (%u) %p (%u)\n", &in[cut_at], (uintptr_t)&in[cut_at] & 7, &in[pivot + 1], (uintptr_t)&in[pivot + 1] & 7);
        // printf("ends: %p (%u) %p (%u)\n", ends[0], (uintptr_t)ends[0] & 7, ends[1], (uintptr_t)ends[1] & 7);
        // printf("Gesamtlänge: %u (%u + %u)\n", ends[0] - &in[cut_at] + 1 + ends[1] - &in[pivot + 1] + 1, ends[0] - &in[cut_at] + 1, ends[1] - &in[pivot + 1] + 1);

        // merge_mram(ptr, ends, &out[border + 1], wram);
    } else {
        handshake_notify();
        handshake_wait_for(yours);

        T __mram_ptr *starts[2] = { &in[from[mine][0].start], &in[from[mine][1].start] };
        T __mram_ptr *ends[2] = { &in[from[mine][0].end], &in[from[mine][1].end] };

        // printf(
        //     "[%u]\n"
        //     "starts: %p (%u) %p (%u)\n"
        //     "ends: %p (%u) %p (%u)\n"
        //     "Gesamtlänge: %u (%u + %u)\n",
        //     me(),
        //     starts[0], *starts[0], starts[1], *starts[1],
        //     ends[0], *ends[0], ends[1], *ends[1],
        //     (ends[0] - starts[0] + 1) + (ends[1] - starts[1] + 1),
        //     ends[0] - starts[0] + 1, ends[1] - starts[1] + 1
        // );

        if ((intptr_t)starts[0] > (intptr_t)ends[0]) {  // The shorter run may be empty.
            flush_run_(starts[1], ends[1], &out[borders[mine]]);
        } else {
            T *ptr[2] = {
                sr_init(buffers[me()].seq_1, starts[0], &sr[me()][0]),
                sr_init(buffers[me()].seq_2, starts[1], &sr[me()][1]),
            };
            merge_mram(ptr, ends, &out[borders[mine]], wram);
        }
    }
    flipped[me()] = !flipped[me()];

    barrier_wait(&omni_barrier);
    print_array(out, buffers[me()].cache, host_to_dpu.length, "Sorted");
}

static void merge_par_(T __mram_ptr * const start, T __mram_ptr * const end) {
    // print_array(input, buffers[me()].cache, host_to_dpu.length, "Input");
    merge_sort_mram(start, end);
    if (me() == NR_TASKLETS - 2) {
        handshake_notify();
        handshake_wait_for(me() + 1);
    } else if (me() == NR_TASKLETS - 1) {
        handshake_wait_for(me() - 1);
        if (flipped[me() - 1] != flipped[me()]) {
            T __mram_ptr *in = (flipped[me()]) ? output : input;
            T __mram_ptr *out = (flipped[me()]) ? input : output;
            copy_run(&in[from[me()][0].start], &in[from[me()][0].end], &out[from[me()][0].start]);
            flipped[me()] = !flipped[me()];
        }
        handshake_notify();
    }
    print_array((flipped[me()]) ? output : input, buffers[me()].cache, host_to_dpu.length, "Starting runs");

    merge_par(from[me()][0].start, from[me()][0].end - 1);
}

union algo_to_test __host algos[] = {
    {{ "MergePar", { .mram = merge_par_ } }},
};
size_t __host num_of_algos = sizeof algos / sizeof algos[0];

int main(void) {
    /* Set up buffers. */
    if (buffers[me()].cache == NULL) {  // Only allocate on the first launch.
        allocate_triple_buffer(&buffers[me()]);
    }
    T * const cache = buffers[me()].cache;

    /* Set up dummy values if called via debugger. */
    if (me() == 0 && host_to_dpu.length == 0) {
        host_to_dpu.reps = 1;
        host_to_dpu.length = 0x7004;  // 0x102
        host_to_dpu.offset = DMA_ALIGNED(host_to_dpu.length * sizeof(T)) / sizeof(T);
        host_to_dpu.part_length = ALIGN(
            DIV_CEIL(host_to_dpu.length, NR_TASKLETS) * sizeof(T),
            16
        ) / sizeof(T);
        host_to_dpu.basic_seed = 0b1011100111010;
        host_to_dpu.algo_index = 0;
        input_rngs[me()] = seed_xs(host_to_dpu.basic_seed + me());
        mram_range range = { 0, host_to_dpu.length * host_to_dpu.reps };
        generate_uniform_distribution_mram(input, cache, &range, 8);
        // generate_reverse_sorted_distribution_mram(input, cache, &range);
    }
    barrier_wait(&omni_barrier);

    /* Perform test. */
    mram_range range = {
        me() * host_to_dpu.part_length,
        (me() == NR_TASKLETS - 1) ? host_to_dpu.offset : (me() + 1) * host_to_dpu.part_length,
    };
    from[me()][0].start = range.start, from[me()][0].end = range.end;
    sort_algo_mram * const algo = algos[host_to_dpu.algo_index].data.fct.mram;
    memset(&dpu_to_host, 0, sizeof dpu_to_host);

    for (uint32_t rep = 0; rep < host_to_dpu.reps; rep++) {
        pivot_rngs[me()] = seed_xs_offset(host_to_dpu.basic_seed + me());

        get_stats_unsorted(input, cache, range, false, &stats_before);

        barrier_wait(&omni_barrier);
        perfcounter_config(COUNT_CYCLES, true);
        time new_time;
        if (me() == 0)
            new_time = perfcounter_get();
        algo(&input[range.start], &input[range.end - 1]);
        if (me() == 0) {
            new_time = perfcounter_get() - new_time - CALL_OVERHEAD;
            dpu_to_host.firsts += new_time;
            dpu_to_host.seconds += new_time * new_time;
        }
        barrier_wait(&omni_barrier);

        T __mram_ptr *sorted_array = (flipped[me()]) ? output : input;
        flipped[me()] = false;  // Following sorting algorithms may not reset this value.
        get_stats_sorted(sorted_array, cache, range, false, &stats_after);
        if (compare_stats(&stats_before, &stats_after, false) == EXIT_FAILURE) {
            // abort();
        }

        range.start += host_to_dpu.offset;
        range.end += host_to_dpu.offset;
        host_to_dpu.basic_seed += NR_TASKLETS;
    }

    return EXIT_SUCCESS;
}
