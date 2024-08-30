/*
This file is heavily based on seqread.inc by UPMEM, which is licensed as follows:

Copyright (c) 2020, UPMEM
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of UPMEM nor the names of its contributors may be
      used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL UPMEM BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dpuconst.h>
#include <seqread.h>

#include "common.h"

// #define PAGE_SIZE (SEQREAD_CACHE_SIZE)
// #define PAGE_ALLOC_SIZE (2 * PAGE_SIZE)
#define PAGE_SIZE (2*SEQREAD_CACHE_SIZE)
#define PAGE_ALLOC_SIZE (PAGE_SIZE)
#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)

#define MRAM_READ_PAGE(from, to) mram_read((__mram_ptr void *)(from), (void *)(to), PAGE_ALLOC_SIZE)

int __builtin_dpu_seqread_get(int, unsigned int, void *, const unsigned int);

T *seqread_init_straight(seqreader_buffer_t cache, __mram_ptr void *mram_addr, seqreader_t *reader)
{
    reader->wram_cache = cache;
    reader->mram_addr = (uintptr_t)(1 << __DPU_MRAM_SIZE_LOG2);

    uintptr_t target_addr = (uintptr_t)mram_addr;
    uintptr_t current_addr = (uintptr_t)reader->mram_addr;
    uintptr_t wram_cache = (uintptr_t)reader->wram_cache;
    uintptr_t mram_offset = target_addr - current_addr;
    if ((mram_offset & PAGE_IDX_MASK) != 0) {
        uintptr_t target_addr_idx_page = target_addr & PAGE_IDX_MASK;
        MRAM_READ_PAGE(target_addr_idx_page, wram_cache);
        mram_offset = target_addr & PAGE_OFF_MASK;
        reader->mram_addr = target_addr_idx_page;
    }
    return (T *)(mram_offset + wram_cache);
}

__mram_ptr T *seqread_tell_straight(T *ptr, seqreader_t *reader) {
    return (__mram_ptr T *)((uintptr_t)reader->mram_addr + ((uintptr_t)ptr & PAGE_OFF_MASK));
}

T *seqread_get_straight(T *ptr, uint32_t inc, seqreader_t *reader) {
    return (T *)__builtin_dpu_seqread_get((uintptr_t)ptr, inc, reader, PAGE_SIZE);
}
