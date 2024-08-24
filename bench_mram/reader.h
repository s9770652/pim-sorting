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
    T *last_elem;
};

static inline void setup_reader(struct reader * const rdr, uintptr_t const buffer) {
    memcpy(
        rdr,
        &(struct reader){
            .buffer = (T *)buffer,
            .buffer_end = (T *)(buffer + READER_SIZE) - 1
        },
        sizeof(struct reader)
    );
}

static inline T *reset_reader(struct reader * const rdr, T __mram_ptr *from, T __mram_ptr *until) {
    rdr->mram = from;
    rdr->mram_end = until;
    mram_read(rdr->mram, rdr->buffer, READER_SIZE);
    rdr->last_elem = rdr->buffer + (rdr->mram_end - rdr->mram);
    return rdr->buffer;
}

static __noinline T *update_reader(struct reader * const rdr) {
    rdr->mram += READER_LENGTH;
    mram_read(rdr->mram, rdr->buffer, READER_SIZE);
    rdr->last_elem -= READER_LENGTH;
    return rdr->buffer;
}

static inline T __mram_ptr *get_reader_mram_address(struct reader * const rdr, T * const ptr) {
    return rdr->mram + (ptr - rdr->buffer);
}

static inline ptrdiff_t elems_left_in_reader(struct reader * const rdr, T * const ptr) {
    return rdr->last_elem - ptr + 1;
}

static inline bool is_ptr_at_last(struct reader * const rdr, T const * const ptr) {
    return (intptr_t)rdr->last_elem <= (intptr_t)ptr;
}
