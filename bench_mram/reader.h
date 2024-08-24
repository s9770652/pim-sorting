#include <attributes.h>
#include <mram.h>

#include "common.h"

struct reader {
    T __mram_ptr *mram;
    T __mram_ptr *mram_end;
    T * const buffer;
    T * const buffer_end;
};

static inline void setup_reader(struct reader * const rdr, uintptr_t const buffer) {
    memcpy(
        rdr,
        &(struct reader){
            .buffer = (T *)buffer,
            .buffer_end = (T *)(buffer + 2 * SEQREAD_CACHE_SIZE) - 1
        },
        sizeof(struct reader)
    );
}

static inline T *reset_reader(struct reader * const rdr, T __mram_ptr *from, T __mram_ptr *until) {
    rdr->mram = from;
    rdr->mram_end = until;
    mram_read(rdr->mram, rdr->buffer, 2 * SEQREAD_CACHE_SIZE);
    return rdr->buffer;
}

static inline T *update_reader(struct reader * const rdr) {
    rdr->mram += 2 * SEQREAD_CACHE_SIZE / sizeof(T);
    mram_read(rdr->mram, rdr->buffer, 2 * SEQREAD_CACHE_SIZE);
    return rdr->buffer;
}

static inline T __mram_ptr *get_reader_mram_address(struct reader * const rdr, T * const ptr) {
    return rdr->mram + (ptr - rdr->buffer);
}
