/*
This file is based on `seqread.inc` from UPMEM’s drivers, which is licensed as follows:

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

#include <atomic_bit.h>
#include <dpuruntime.h>

#include "buffers.h"
#include "common.h"

#define PAGE_OFF_MASK (PAGE_SIZE - 1)
#define PAGE_IDX_MASK (~PAGE_OFF_MASK)

#if (STRAIGHT_READER == READ_OPT)  // The straight reader uses the full buffer.

#define PAGE_SIZE (2 * SEQREAD_CACHE_SIZE)
#define PAGE_ALLOC_SIZE (PAGE_SIZE)

#elif (STRAIGHT_READER != 0)  // The straight reader uses only the first half of the buffer.

#define PAGE_SIZE (SEQREAD_CACHE_SIZE)
#define PAGE_ALLOC_SIZE (2 * PAGE_SIZE)

#endif

extern void *mem_alloc_nolock(size_t size);
ATOMIC_BIT_EXTERN(__heap_pointer);


void allocate_triple_buffer(triple_buffers *buffers) {
    ATOMIC_BIT_ACQUIRE(__heap_pointer);

    // Initialise a local cache to store one MRAM block.
    T *cache = mem_alloc_nolock(CACHE_SIZE);

    // Initialise the buffers for the two sequential readers.
    uintptr_t heap_pointer = (uintptr_t)__HEAP_POINTER;
    uintptr_t seqs = (heap_pointer + PAGE_OFF_MASK) & PAGE_IDX_MASK;
    size_t size = seqs + 2 * PAGE_ALLOC_SIZE - heap_pointer;
    mem_alloc_nolock(size);

    ATOMIC_BIT_RELEASE(__heap_pointer);

    buffers->cache = cache;
    buffers->seq_1 = seqs;
    buffers->seq_2 = seqs + PAGE_ALLOC_SIZE;
}
