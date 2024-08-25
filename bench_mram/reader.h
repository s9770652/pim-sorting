#include <attributes.h>
#include <mram.h>

#include "common.h"

#define READER_SIZE (2 * SEQREAD_CACHE_SIZE)
#define READER_LENGTH (READER_SIZE >> DIV)

struct reader {
    T __mram_ptr *from;
    T __mram_ptr *until;
    T * const buffer;
    T * const buffer_end;
    T * const buffer_early_end;
    T *last_elem;
    T *ptr;
    T val;
};

static inline void setup_reader(struct reader * const rdr, uintptr_t const buffer,
        size_t const buffer_early_end) {
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

static inline void reset_reader(struct reader * const rdr, T __mram_ptr *from,
        T __mram_ptr *until) {
    rdr->from = from;
    rdr->until = until;
    mram_read(rdr->from, rdr->buffer, READER_SIZE);
    rdr->last_elem = rdr->buffer + (rdr->until - rdr->from);
    rdr->ptr = rdr->buffer;
    rdr->val = *rdr->ptr;
}

static inline T get_reader_value(struct reader * const rdr) {
    return rdr->val;
}

static inline void update_reader_partially(struct reader * const rdr) {
    rdr->val = *++rdr->ptr;
}

static inline void update_reader_fully(struct reader * const rdr) {
    if (rdr->ptr < rdr->buffer_end) {
        update_reader_partially(rdr);
        return;
    }
    rdr->from += READER_LENGTH;
    mram_read(rdr->from, rdr->buffer, READER_SIZE);
    rdr->last_elem -= READER_LENGTH;  // gets optimised away if not needed
    rdr->ptr = rdr->buffer;
    rdr->val = *rdr->ptr;
}

static inline T __mram_ptr *get_reader_mram_address(struct reader * const rdr) {
    return rdr->from + (rdr->ptr - rdr->buffer);
}

static inline ptrdiff_t elems_left_in_reader(struct reader * const rdr) {
    return rdr->last_elem - rdr->ptr + 1;
}

static inline bool is_ptr_at_last(struct reader * const rdr) {
    return (intptr_t)rdr->last_elem <= (intptr_t)rdr->ptr;
}

static inline bool is_early_buffer_end_reached(struct reader * const rdr) {
    return rdr->ptr > rdr->buffer_early_end;
}
