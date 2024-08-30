#include "merge_mram.h"
#include "wram_sorts.h"

void form_starting_runs(T __mram_ptr * const start, T __mram_ptr * const end) {
    T * const cache = buffers[me()].cache;
    T __mram_ptr *i;
    size_t curr_length, curr_size;
    mram_range_ptr range = { start, end + 1 };
    LOOP_BACKWARDS_ON_MRAM_BL(i, curr_length, curr_size, range, STARTING_RUN_LENGTH) {
#if (STARTING_RUN_SIZE > 2048)
        mram_read_triple(i, cache, curr_size);
        quick_sort_wram(cache, cache + curr_length - 1);
        mram_write_triple(cache, i, curr_size);
#else
        mram_read(i, cache, curr_size);
        quick_sort_wram(cache, cache + curr_length - 1);
        mram_write(cache, i, curr_size);
#endif
    }
}
