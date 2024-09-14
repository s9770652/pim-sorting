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

#ifndef _READER_H_
#define _READER_H_

#include <seqread.h>

#include "common.h"

#if (STRAIGHT_READER == READ_OPT)

#define PAGE_SIZE (2 * SEQREAD_CACHE_SIZE)
#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)
#if (PAGE_SIZE == 2048)
#define CARRY_FLAG "nc11"
#elif (PAGE_SIZE == 1024)
#define CARRY_FLAG "nc10"
#elif (PAGE_SIZE == 512)
#define CARRY_FLAG "nc9"
#elif (PAGE_SIZE == 256)
#define CARRY_FLAG "nc8"
#elif (PAGE_SIZE == 128)
#define CARRY_FLAG "nc7"
#elif (PAGE_SIZE == 64)
#define CARRY_FLAG "nc6"
#elif (PAGE_SIZE == 32)
#define CARRY_FLAG "nc5"
#endif

/**
 * @brief Equivalent to `seqread_init`.
 * Sets the WRAM and MRAM addresses of a reader and loads the data.
 * Removed checks for whether the data is already present.
 * @internal For some reason, using `seqread_seek` broke MergeSort on reverse sorted inputs.
 * Since it is never used, it was combined with `seqread_init`.
 * 
 * @param cache The WRAM buffer of the sequential reader.
 * @param mram The first MRAM address to read.
 * @param reader The reader to initialise.
 * 
 * @return The WRAM buffer address of the first MRAM item.
**/
static inline T *sr_init(seqreader_buffer_t cache, void __mram_ptr *mram, seqreader_t *reader) {
    reader->mram_addr = (uintptr_t)mram & PAGE_IDX_MASK;
    mram_read((void __mram_ptr *)reader->mram_addr, (void *)cache, PAGE_SIZE);
    return (T *)(cache + ((uintptr_t)mram & PAGE_OFF_MASK));
}

/**
 * @brief Equivalent to `seqreader_tell`.
 * Returns the MRAM address of a given item within the buffer.
 * @internal The MRAM addresses of readers are now detached from the reader strucs,
 * which now serve only for initialisation purposes.
 * 
 * @param ptr The pointer to the item in the buffer.
 * @param reader (unused)
 * @param mram The current MRAM address of the reader of the address.
 * 
 * @return The MRAM address of the given item.
**/
static inline T __mram_ptr *sr_tell(T *ptr, seqreader_t *reader, uintptr_t mram) {
    (void)reader;
    return (T __mram_ptr *)(mram + ((uintptr_t)ptr & PAGE_OFF_MASK));
}

/**
 * @brief Equivalent to `seqreader_get`.
 * Advances the pointer to the current item and reloads if needed.
 * @attention This is a macro, not a function! It updates the pointer on its own.
 * @internal
 * 1. Advance pointer and performs bounds check. If negative, skip over the next three lines.
 * 2. Increase the MRAM address.
 * 3. Load the new data.
 * 4. Reset the pointer.
 * 
 * @param ptr The *identifier* of the pointer to the current item.
 * @param reader (unused)
 * @param mram The *identifier* of the MRAM address of the respective reader.
 * @param wram The *identifier* of the WRAM address of the respective reader.
**/
#define SR_GET(ptr, reader, mram, wram)                      \
__asm__ volatile(                                            \
    "add %[p], %[p], 1 << "__STR(DIV)", "CARRY_FLAG", .+4\n" \
    "add %[m], %[m], "__STR(PAGE_SIZE)"\n"                   \
    "ldma %[w], %[m], "__STR(PAGE_SIZE)"/8 - 1\n"            \
    "add %[p], %[p], -"__STR(PAGE_SIZE)                      \
    : "+r"(ptr), "+r"(mram)                                  \
    : [p] "r"(ptr), [m] "r"(mram), [w] "r"(wram)             \
)

#undef CARRY_FlAG

#elif (STRAIGHT_READER == READ_STRAIGHT)

#include <mram.h>
#include <dpuconst.h>
#include <dpuruntime.h>
#include <atomic_bit.h>
#include <stddef.h>

#define PAGE_SIZE (SEQREAD_CACHE_SIZE)
#define PAGE_ALLOC_SIZE (2 * PAGE_SIZE)
#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)

#define MRAM_READ_PAGE(from, to) mram_read((__mram_ptr void *)(from), (void *)(to), PAGE_ALLOC_SIZE)

/**
 * @brief Equivalent to `seqread_init`.
 * 
 * @param cache The WRAM buffer of the sequential reader.
 * @param mram The first MRAM address to read.
 * @param reader The reader to initialise.
 * 
 * @return The WRAM buffer address of the first MRAM item.
**/
static inline T *sr_init(seqreader_buffer_t cache, void __mram_ptr *mram, seqreader_t *reader) {
    reader->wram_cache = cache;
    reader->mram_addr = (uintptr_t)(1 << __DPU_MRAM_SIZE_LOG2);

    uintptr_t target_addr = (uintptr_t)mram;
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

/**
 * @brief Equivalent to `seqreader_tell`.
 * 
 * @param ptr The pointer to the item in the buffer.
 * @param reader The reader of the respective item.
 * @param mram (unused)
 * 
 * @return The MRAM address of the given item.
**/
static inline T __mram_ptr *sr_tell(void *ptr, seqreader_t *reader, uintptr_t mram) {
    (void)mram;
    return (__mram_ptr void *)((uintptr_t)reader->mram_addr + ((uintptr_t)ptr & PAGE_OFF_MASK));
}

/**
 * @brief Equivalent to `ptr = seqreader_get(…)`.
 * @attention This is a macro, not a function! It updates the pointer on its own.
 * 
 * @param ptr The *identifier* of the pointer to the current item.
 * @param reader The *address* of reader of the respective item.
 * @param mram (unused)
 * @param wram (unused)
**/
#define SR_GET(ptr, reader, mram, wram)                                        \
ptr = (T *)__builtin_dpu_seqread_get((uintptr_t)ptr, sizeof(T), reader, PAGE_SIZE);

#elif (STRAIGHT_READER == READ_REGULAR)

/**
 * @brief Equivalent to `seqread_init`.
 * 
 * @param cache The WRAM buffer of the sequential reader.
 * @param mram The first MRAM address to read.
 * @param reader The reader to initialise.
 * 
 * @return The WRAM buffer address of the first MRAM item.
**/
static inline T *sr_init(seqreader_buffer_t cache, void __mram_ptr *mram,  seqreader_t *reader) {
    return seqread_init(cache, mram, reader);
}

/**
 * @brief Equivalent to `seqreader_tell`.
 * 
 * @param ptr The pointer to the item in the buffer.
 * @param reader The reader of the respective item.
 * @param mram (unused)
 * 
 * @return The MRAM address of the given item.
**/
static inline T __mram_ptr *sr_tell(void *ptr, seqreader_t *reader, uintptr_t mram) {
    (void)mram;
    return seqread_tell(ptr, reader);
}

/**
 * @brief Equivalent to `ptr = seqreader_get(…)`.
 * @attention This is a macro, not a function! It updates the pointer on its own.
 * 
 * @param ptr The *identifier* of the pointer to the current item.
 * @param reader The *address* of reader of the respective item.
 * @param mram (unused)
 * @param wram (unused)
**/
#define SR_GET(ptr, reader, mram, wram) ptr = seqread_get(ptr, sizeof(T), reader)

#endif  // STRAIGHT_READER == READ_REGULAR

#endif  // _READER_H_
