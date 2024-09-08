#include "common.h"
#include "reader.h"

void merge_mram(T *ptr[2], T __mram_ptr * const ends[2], T __mram_ptr *out,
        seqreader_buffer_t wram[2]);