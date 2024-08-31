/*
This file is heavily based on `seqread.inc` from UPMEM’s drivers, which is licensed as follows:

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

/**
 * @file
 * @brief A reimplementation of sequential readers
 * to get everything straight away without function calls.
**/

#include <dpuconst.h>
#include <seqread.h>

#include "common.h"

#define PAGE_SIZE (2 * SEQREAD_CACHE_SIZE)
#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)

/**
 * @brief Equivalent to `seqread_init`.
 * Sets the WRAM and MRAM addresses of a reader and loads the data.
 * @internal For some reason, using `seqread_seek` broke MergeSort on reverse sorted inputs.
 * Since it is never used, it was combined with `seqread_init`.
 * 
 * @param cache The WRAM buffer of the sequential reader.
 * @param mram The first MRAM address to read.
 * @param reader The reader to initialise.
 * 
 * @return The WRAM buffer address of the first MRAM item.
**/
T *seqread_init_straight(seqreader_buffer_t cache, void __mram_ptr *mram, seqreader_t *reader) {
    reader->mram_addr = (uintptr_t)(1 << __DPU_MRAM_SIZE_LOG2);

    uintptr_t target_addr = (uintptr_t)mram;
    uintptr_t current_addr = (uintptr_t)reader->mram_addr;
    uintptr_t mram_offset = target_addr - current_addr;
    if ((mram_offset & PAGE_IDX_MASK) != 0) {
        uintptr_t target_addr_idx_page = target_addr & PAGE_IDX_MASK;
        mram_read((void __mram_ptr *)(target_addr_idx_page), (void *)(cache), PAGE_SIZE);
        mram_offset = target_addr & PAGE_OFF_MASK;
        reader->mram_addr = target_addr_idx_page;
    }
    return (T *)(mram_offset + cache);
}

/**
 * @brief Equivalent to `seqreader_tell`.
 * Returns the MRAM address of a given item within the buffer.
 * @internal The MRAM addresses of readers are now detached from the reader strucs,
 * which now serve only for initialisation purposes.
 * 
 * @param ptr The pointer to the item in the buffer.
 * @param mram The current MRAM address of the reader of the address.
 * 
 * @return The MRAM address of the given item.
**/
T __mram_ptr *seqread_tell_straight(T *ptr, uintptr_t mram) {
    return (T __mram_ptr *)(mram + ((uintptr_t)ptr & PAGE_OFF_MASK));
}

/**
 * @brief Equivalent to `seqreader_get`.
 * Advances the pointer to the current item and reloads if needed.
 * @attention This is a macro, not a function!
 * 
 * @param ptr The *identifier* of the pointer to the current item.
 * @param mram The *identifier* of the MRAM address of the respective reader.
 * @param wram The *identifier* of the WRAM address of the respective reader.
**/
#define SEQREAD_GET_STRAIGHT(ptr, mram, wram)    \
__asm__ volatile(                                \
    "add %[p], %[p], 4, nc10, .+4\n"             \
    "add %[m], %[m], 1024\n"                     \
    "ldma %[w], %[m], 127\n"                     \
    "add %[p], %[p], -1024"                      \
    : "+r"(ptr), "+r"(mram)                      \
    : [p] "r"(ptr), [m] "r"(mram), [w] "r"(wram) \
)
