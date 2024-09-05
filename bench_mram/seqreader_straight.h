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

#ifndef _SEQREADER_STRAIGHT_H_
#define _SEQREADER_STRAIGHT_H_

#include <seqread.h>

#include "common.h"

#if STRAIGHT_READER

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
 * Starting to load right from the start, that is, the page model is forgone.
 * Removed checks for whether the data is already present.
 * @attention It is assumed that the MRAM address is properly aligned!
 * @internal For some reason, using `seqread_seek` broke MergeSort on reverse sorted inputs.
 * Since it is never used, it was combined with `seqread_init`.
 * 
 * @param cache The WRAM buffer of the sequential reader.
 * @param mram The first MRAM address to read.
 * @param reader The reader to initialise.
 * 
 * @return The WRAM buffer address of the first MRAM item.
**/
T *sr_init(seqreader_buffer_t cache, void __mram_ptr *mram, seqreader_t *reader) {
    reader->mram_addr = (uintptr_t)mram;
    mram_read((void __mram_ptr *)reader->mram_addr, (void *)cache, PAGE_SIZE);
    return (T *)cache;
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
 * @param wram The beginning of the buffer of the respective reader.
 * 
 * @return The MRAM address of the given item.
**/
T __mram_ptr *sr_tell(T *ptr, seqreader_t *reader, uintptr_t mram, seqreader_buffer_t wram) {
    (void)reader;
    return (T __mram_ptr *)(mram + ((uintptr_t)ptr - wram));
}

/**
 * @brief Equivalent to `seqreader_get`.
 * Advances the pointer to the current item and reloads if needed.
 * @attention This is a macro, not a function!
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

#else  // STRAIGHT_READER

/**
 * @brief Equivalent to `seqread_init`.
 * 
 * @param cache The WRAM buffer of the sequential reader.
 * @param mram The first MRAM address to read.
 * @param reader The reader to initialise.
 * 
 * @return The WRAM buffer address of the first MRAM item.
**/
T *sr_init(seqreader_buffer_t cache, void __mram_ptr *mram, seqreader_t *reader) {
    return seqread_init(cache, mram, reader);
}

/**
 * @brief Equivalent to `seqreader_tell`.
 * 
 * @param ptr The pointer to the item in the buffer.
 * @param reader The reader of the respective item.
 * @param mram (unused)
 * @param wram (unused)
 * 
 * @return The MRAM address of the given item.
**/
T __mram_ptr *sr_tell(void *ptr, seqreader_t *reader, uintptr_t mram, seqreader_buffer_t wram) {
    (void)mram, (void)wram;
    return seqread_tell(ptr, reader);
}

/**
 * @brief Equivalent to `ptr = seqreader_get(…)`.
 * @attention This is a macro, not a function!
 * 
 * @param ptr The *identifier* of the pointer to the current item.
 * @param reader The *address* of reader of the respective item.
 * @param mram (unused)
 * @param wram (unused)
**/
#define SR_GET(ptr, reader, mram, wram) ptr = seqread_get(ptr, sizeof(T), reader)

#endif  // STRAIGHT_READER

#endif  // _SEQREADER_STRAIGHT_H_
