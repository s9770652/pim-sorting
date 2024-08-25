/**
 * @file
 * @brief Faster sequential reading of items in MRAM.
**/

#ifndef _READER_H_
#define _READER_H_

#include <assert.h>
#include <attributes.h>
#include <mram.h>

#include "common.h"

/// @brief How many bytes the sequential reader reads at once.
/// @todo Does reading less if the end is reached improve the performance?
#define READER_SIZE (2 * SEQREAD_CACHE_SIZE)
/// @brief How many items the sequential reader reads at once.
#define READER_LENGTH (READER_SIZE >> DIV)
static_assert(
    !(READER_SIZE % sizeof(T)),
    "Custom reader buffers must be capable of holding a whole multiple of numbers!"
);

/**
 * @brief A custom sequential reader which accepts a beginning and an end of an MRAM array.
 * It also supports cheap reload checks. Uses a WRAM buffer of size `READER_SIZE`.
**/
struct reader {
    /// @brief The first item of the WRAM buffer.
    T * const buffer;
    /// @brief The last item of the WRAM buffer.
    T * const buffer_end;
    /// @brief The item with a distance to the end of the WRAM buffer specified during set-up.
    T * const buffer_early_end;
    /// @brief The next MRAM item to load.
    T __mram_ptr *from;
    /// @brief The last MRAM item to load.
    T __mram_ptr *until;
    /// @brief The address of the current item in the WRAM buffer.
    T *ptr;
    /// @brief The value of the current item in the WRAM buffer.
    T val;
    /// @brief The hypothetical WRAM address of the last MRAM item,
    /// had the whole remainder of the range been loaded.
    T *last_item;
};

/**
 * @brief Registers the WRAM buffer of a sequential reader. Must only be called once.
 * @todo Static definition of buffers.
 * 
 * @param reader The reader whose buffer to set.
 * @param buffer The address of the buffer.
 * @param early_end_distance The distance between the end and the early end of the buffer.
**/
static inline void setup_reader(struct reader * const reader, uintptr_t const buffer,
        size_t const early_end_distance) {
    memcpy(
        reader,
        &(struct reader){
            .buffer = (T *)buffer,
            .buffer_end = (T *)(buffer + READER_SIZE) - 1,
            .buffer_early_end = (T *)(buffer + READER_SIZE) - 1 - early_end_distance,
        },
        sizeof(struct reader)
    );
}

/**
 * @brief Specifying a new MRAM array to read.
 * 
 * @param reader The reader with which to read from the array.
 * @param from The first MRAM item to read.
 * @param until The last MRAM item to read.
**/
static inline void reset_reader(struct reader * const reader, T __mram_ptr *from,
        T __mram_ptr *until) {
    reader->from = from;
    reader->until = until;
    mram_read(reader->from, reader->buffer, READER_SIZE);
    reader->ptr = reader->buffer;
    reader->val = *reader->ptr;
    reader->last_item = reader->buffer + (reader->until - reader->from);
}

/**
 * @brief The current item in the WRAM buffer.
 * 
 * @param reader The reader of the respective buffer.
 * 
 * @return The value of the current item.
**/
static inline T get_reader_value(struct reader * const reader) {
    return reader->val;
}

/**
 * @brief Advancing the pointer on the current item without reloading.
 * @sa is_early_end_reached
 * @sa update_reader_fully
 * 
 * @param reader The reader of the respective pointer.
**/
static inline void update_reader_partially(struct reader * const reader) {
    reader->val = *++reader->ptr;
}

/**
 * @brief Advancing the pointer on the current item with potential reloading.
 * @sa is_early_end_reached
 * @sa update_reader_partially
 * 
 * @param reader The reader of the respective pointer.
**/
static inline void update_reader_fully(struct reader * const reader) {
    if (reader->ptr < reader->buffer_end) {
        update_reader_partially(reader);
        return;
    }
    reader->from += READER_LENGTH;
    mram_read(reader->from, reader->buffer, READER_SIZE);
    reader->ptr = reader->buffer;
    reader->val = *reader->ptr;
    reader->last_item -= READER_LENGTH;  // optimised away if not needed
}

/**
 * @brief The MRAM address of the current item in the WRAM buffer.
 * 
 * @param reader The reader of the respective buffer.
 * 
 * @return The MRAM address.
**/
static inline T __mram_ptr *get_reader_mram_address(struct reader * const reader) {
    return reader->from + (reader->ptr - reader->buffer);
}

/**
 * @brief Calculating how many items must still be read,
 * including all remaining items in the MRAM array, the current item in the buffer,
 * and the items behind the current item.
 * 
 * @param reader The reader of the respective array.
 * 
 * @return The number of unread items.
**/
static inline ptrdiff_t items_left_in_reader(struct reader * const reader) {
    return reader->last_item - reader->ptr + 1;  // `+1` optimised away
}

/**
 * @brief Checks whether the current item is the last item or even beyond that.
 * 
 * @param reader The reader of the respective items.
 * 
 * @return `true` if the last item is or was the current item, otherwise `false`.
**/
static inline bool was_last_item_read(struct reader * const reader) {
    return (intptr_t)reader->last_item <= (intptr_t)reader->ptr;
}

/**
 * @brief Checks whether the current item is beyond the early end of its buffer.
 * If so, `update_reader_fully` should be used, otherwise `update_reader_partially` is fine.
 * @sa update_reader_partially
 * @sa update_reader_fully
 * 
 * @param reader The reader of the respective buffer.
 * 
 * @return `true` if the current item has surpassed the early end, otherwise `false`.
**/
static inline bool is_early_end_reached(struct reader * const reader) {
    return reader->ptr > reader->buffer_early_end;
}

#endif  // _READER_H_
