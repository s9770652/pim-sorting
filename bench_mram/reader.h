#include <attributes.h>
#include <mram.h>

#include "common.h"

#define READER_SIZE (2 * SEQREAD_CACHE_SIZE)
#define READER_LENGTH (READER_SIZE >> DIV)

struct reader {
    T __mram_ptr *mram;
    T __mram_ptr *mram_end;
    T * const buffer;
    T * const buffer_end;
    T * const buffer_early_end;
    T *last_elem;
    T *ptr;
};

static inline void setup_reader(struct reader * const rdr, uintptr_t const buffer, size_t const buffer_early_end) {
    memcpy(
        rdr,
        &(struct reader){
            .buffer = (T *)buffer,
            .buffer_end = (T *)(buffer + READER_SIZE) - 1,
            .buffer_early_end = (T *)(buffer + READER_SIZE) - 1 - buffer_early_end,
        },
        sizeof(struct reader)
    );
}

static inline void reset_reader(struct reader * const rdr, T __mram_ptr *from, T __mram_ptr *until) {
    rdr->mram = from;
    rdr->mram_end = until;
    mram_read(rdr->mram, rdr->buffer, READER_SIZE);
    rdr->last_elem = rdr->buffer + (rdr->mram_end - rdr->mram);
    rdr->ptr = rdr->buffer;
}

static inline T *update_reader_partially(struct reader * const rdr) {
    return ++rdr->ptr;
}

static inline T *update_reader_fully(struct reader * const rdr) {
    if (rdr->ptr < rdr->buffer_end) {
        return update_reader_partially(rdr);
    }
    rdr->mram += READER_LENGTH;
    mram_read(rdr->mram, rdr->buffer, READER_SIZE);
    rdr->last_elem -= READER_LENGTH;  // gets optimised away if not needed
    return (rdr->ptr = rdr->buffer);
}

static inline T __mram_ptr *get_reader_mram_address(struct reader * const rdr) {
    return rdr->mram + (rdr->ptr - rdr->buffer);
}

static inline ptrdiff_t elems_left_in_reader(struct reader * const rdr) {
    return rdr->last_elem - rdr->ptr + 1;
}

static inline bool is_ptr_at_last(struct reader * const rdr) {
    return (intptr_t)rdr->last_elem <= (intptr_t)rdr->ptr;
}
