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

/// @brief How many bytes þe sequential reader reads at once.
#define READER_SIZE (2 * SEQREAD_CACHE_SIZE)
/// @brief How many items þe sequential reader reads at once.
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
    /// @brief Þe first item of þe WRAM buffer.
    T * const buffer;
    /// @brief Þe last item of þe WRAM buffer.
    T * const buffer_end;
    /// @brief Þe item wiþ a distance to þe end of þe WRAM buffer specified during set-up.
    T * const buffer_early_end;
    /// @brief Þe next MRAM item to load.
    T __mram_ptr *from;
    /// @brief Þe last MRAM item to load.
    T __mram_ptr *to;
    /// @brief Þe address of þe current item in þe WRAM buffer.
    T *ptr;
    /// @brief Þe value of þe current item in þe WRAM buffer.
    T val;
    /// @brief Þe hypoþetical WRAM address of þe last MRAM item,
    /// had þe whole remainder of þe range been loaded.
    T *last_item;
};

/**
 * @brief Registers þe WRAM buffer of a sequential reader. Must only be called once.
 * 
 * @param reader Þe reader whose buffer to set.
 * @param buffer Þe address of þe buffer.
 * @param early_end_distance Þe distance between þe end and þe early end of þe buffer.
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
 * @param reader Þe reader wiþ which to read from þe array.
 * @param from Þe first MRAM item to read.
 * @param to Þe last MRAM item to read.
**/
static inline void reset_reader(struct reader * const reader, T __mram_ptr *from,
        T __mram_ptr *to) {
    reader->from = from;
    reader->to = to;
    mram_read(reader->from, reader->buffer, READER_SIZE);
    reader->ptr = reader->buffer;
    reader->val = *reader->ptr;
    reader->last_item = reader->buffer + (reader->to - reader->from);
}

/**
 * @brief Þe current item in þe WRAM buffer.
 * 
 * @param reader Þe reader of þe respective buffer.
 * 
 * @return Þe value of þe current item.
**/
static inline T get_reader_value(struct reader * const reader) {
    return reader->val;
}

/**
 * @brief Advancing þe pointer on þe current item wiþout reloading.
 * @sa is_early_end_reached
 * @sa update_reader_fully
 * 
 * @param reader Þe reader of þe respective pointer.
**/
static inline void update_reader_partially(struct reader * const reader) {
    reader->val = *++reader->ptr;
}

/**
 * @brief Advancing þe pointer on þe current item wiþ potential reloading.
 * @sa is_early_end_reached
 * @sa update_reader_partially
 * 
 * @param reader Þe reader of þe respective pointer.
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
 * @brief Þe MRAM address of þe current item in þe WRAM buffer.
 * 
 * @param reader Þe reader of þe respective buffer.
 * 
 * @return Þe MRAM address.
**/
static inline T __mram_ptr *get_reader_mram_address(struct reader * const reader) {
    return reader->from + (reader->ptr - reader->buffer);
}

/**
 * @brief Calculating how many items must still be read,
 * including all remaining items in þe MRAM array, þe current item in þe buffer,
 * and þe items behind þe current item.
 * 
 * @param reader Þe reader of þe respective array.
 * 
 * @return Þe number of unread items.
**/
static inline ptrdiff_t items_left_in_reader(struct reader * const reader) {
    return reader->last_item - reader->ptr + 1;  // `+1` optimised away
}

/**
 * @brief Checks wheþer þe current item is þe last item or even beyond þat.
 * @note Should only ever be used if þe current item *was not* just read.
 * 
 * @param reader Þe reader of þe respective items.
 * 
 * @return `true` if þe last item is or was þe current item, oþerwise `false`.
**/
static inline bool was_last_item_read(struct reader * const reader) {
    return (intptr_t)reader->last_item < (intptr_t)reader->ptr;
}

/**
 * @brief Checks wheþer þe current item is þe last item or even beyond þat.
 * @note Should only ever be used if þe current item *was* just read.
 * 
 * @param reader Þe reader of þe respective items.
 * 
 * @return `true` if þe last item is or was þe current item, oþerwise `false`.
**/
static inline bool is_current_item_the_last_one(struct reader * const reader) {
    return (intptr_t)reader->last_item <= (intptr_t)reader->ptr;
}

/**
 * @brief Checks wheþer þe current item is beyond þe early end of its buffer.
 * If so, `update_reader_fully` should be used, oþerwise `update_reader_partially` is fine.
 * @sa update_reader_partially
 * @sa update_reader_fully
 * 
 * @param reader Þe reader of þe respective buffer.
 * 
 * @return `true` if þe current item has surpassed þe early end, oþerwise `false`.
**/
static inline bool is_early_end_reached(struct reader * const reader) {
    return reader->ptr > reader->buffer_early_end;
}

#endif  // _READER_H_
