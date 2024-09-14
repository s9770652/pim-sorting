#include <stddef.h>

#include "communication.h"
#include "mram_sorts.h"
#include "starting_runs.h"

#define MRAM_MERGE FULL_SPACE
#include "mram_merging_aligned.h"

extern T __mram_ptr output[];

extern bool flipped[NR_TASKLETS];  // Whether `output` contains the latest sorted runs.

void merge_sort_mram(T __mram_ptr * const start, T __mram_ptr * const end) {
    /* Starting runs. */
    form_starting_runs(start, end);

    /* Merging. */
    seqreader_buffer_t const wram[2] = { buffers[me()].seq_1, buffers[me()].seq_2 };
    T __mram_ptr *in, *until, *out;  // Runs from `in` to `until` are merged and stored at `out`.
    bool flip = false;  // Used to determine the initial positions of `in`, `out`, and `until`.
    size_t const n = end - start + 1;
    for (size_t run_length = STARTING_RUN_LENGTH; run_length < n; run_length *= 2) {
        // Set the positions to read from and write to.
        if ((flip = !flip)) {
            in = start;
            until = end;
            out = (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output) + n;
        } else {
            in = (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output);
            until = (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output) + n - 1;
            out = end + 1;
        }
        // Merge pairs of neighboured runs.
        T __mram_ptr *run_1_end = until - run_length;
        for (; (intptr_t)run_1_end >= (intptr_t)in; run_1_end -= 2*run_length) {
            T __mram_ptr *run_1_start;
            if ((intptr_t)(run_1_end + 1 - run_length) >= (intptr_t)in) {
                run_1_start = run_1_end + 1 - run_length;
                out -= 2*run_length;
            } else {
                run_1_start = in;
                out -= run_length + (run_1_end - run_1_start + 1);
            }
            T __mram_ptr * const ends[2] = { run_1_end, run_1_end + run_length };
            T *ptr[2] = {
                sr_init(buffers[me()].seq_1, run_1_start, &sr[me()][0]),
                sr_init(buffers[me()].seq_2, run_1_end + 1, &sr[me()][1]),
            };
            merge_mram_aligned(ptr, ends, out, wram);
        }
        // Flush single run at the beginning straight away
        if ((intptr_t)(run_1_end + run_length) >= (intptr_t)in) {
            out = (flip) ? (T __mram_ptr *)((uintptr_t)start + (uintptr_t)output) : start;
            copy_run(in, run_1_end + run_length, out);
        }
    }
    flipped[me()] = flip;
}
