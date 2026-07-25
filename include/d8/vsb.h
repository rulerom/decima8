/*
 * DECIMA-8 Core - Pure C Implementation
 * VSB (Vector Stream Buffer) format
 *
 * VSB file format (IDE compatible):
 * - Each frame: 4 bytes
 * - Each byte contains 2 channels (4-bit nibbles)
 * - Byte 0: channels 0-1 (low=ch0, high=ch1)
 * - Byte 1: channels 2-3
 * - Byte 2: channels 4-5
 * - Byte 3: channels 6-7
 * - Total: 4 bytes = 8 channels (0..15 each)
 *
 * Tagged format (for sequences with frame tags):
 * - Each frame: 8 bytes (4-byte tag + 4-byte data)
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define D8_VSB_FRAME_SIZE 4U   /* 4 bytes per frame (IDE format) */
#define D8_VSB_TAGGED_FRAME_SIZE 8U  /* 4-byte tag + 4-byte data */
#define D8_VSB_MAX_FRAMES 200000U

/* ============================================================================
 * VSB Frame structure
 * ============================================================================ */

typedef struct {
    d8_u32 frame_tag;
    d8_u8  data[D8_K_LANES];
} d8_vsb_frame_t;

/* ============================================================================
 * VSB Tape (collection of frames)
 * ============================================================================ */

typedef struct {
    d8_vsb_frame_t frames[D8_VSB_MAX_FRAMES];
    size_t count;
} d8_vsb_tape_t;

/* ============================================================================
 * VSB Loading
 * ============================================================================ */

/**
 * @brief Load VSB tape from file
 * 
 * File format options:
 * 1. Raw 8-byte frames (no tags): each frame is 8 bytes, frame_tag = index
 * 2. Tagged frames: each frame is 12 bytes (4-byte tag + 8-byte data)
 * 
 * The format is auto-detected by file size:
 * - If file_size % 12 == 0: tagged format
 * - If file_size % 8 == 0: raw format
 *
 * @param path Path to VSB file
 * @param tape Output tape structure
 * @return 0 on success, -1 on error
 */
int d8_vsb_load(const char* path, d8_vsb_tape_t* tape);

/**
 * @brief Load VSB tape from memory buffer
 * 
 * @param data Pointer to data buffer
 * @param size Size of data in bytes
 * @param tagged If non-zero, expect 12-byte frames (tag+data), else 8-byte raw
 * @param tape Output tape structure
 * @return 0 on success, -1 on error
 */
int d8_vsb_load_from_memory(const d8_u8* data, size_t size, int tagged,
                             d8_vsb_tape_t* tape);

/**
 * @brief Save VSB tape to file (tagged format)
 * 
 * @param path Path to output file
 * @param tape Pointer to tape structure
 * @return 0 on success, -1 on error
 */
int d8_vsb_save(const char* path, const d8_vsb_tape_t* tape);

/**
 * @brief Get frame tag for a given index
 * 
 * @param tape Pointer to tape
 * @param index Frame index (0..count-1)
 * @return Frame tag, or 0 if index out of range
 */
static inline d8_u32 d8_vsb_get_frame_tag(const d8_vsb_tape_t* tape, size_t index) {
    if (index >= tape->count) return 0;
    return tape->frames[index].frame_tag;
}

/**
 * @brief Get frame data for a given index
 * 
 * @param tape Pointer to tape
 * @param index Frame index (0..count-1)
 * @param out_data Output buffer (must be at least 8 bytes)
 * @return 0 on success, -1 if index out of range
 */
static inline int d8_vsb_get_frame_data(const d8_vsb_tape_t* tape, size_t index,
                                         d8_u8 out_data[D8_K_LANES]) {
    if (index >= tape->count) return -1;
    for (size_t i = 0; i < D8_K_LANES; i++) {
        out_data[i] = tape->frames[index].data[i];
    }
    return 0;
}

/**
 * @brief Set frame data for a given index
 * 
 * @param tape Pointer to tape
 * @param index Frame index (0..count-1)
 * @param frame_tag Frame tag
 * @param data Input data (8 bytes)
 * @return 0 on success, -1 if index out of range
 */
static inline int d8_vsb_set_frame(d8_vsb_tape_t* tape, size_t index,
                                    d8_u32 frame_tag, const d8_u8 data[D8_K_LANES]) {
    if (index >= tape->count) return -1;
    tape->frames[index].frame_tag = frame_tag;
    for (size_t i = 0; i < D8_K_LANES; i++) {
        tape->frames[index].data[i] = data[i];
    }
    return 0;
}

/**
 * @brief Add a new frame to the tape
 * 
 * @param tape Pointer to tape
 * @param frame_tag Frame tag
 * @param data Input data (8 bytes)
 * @return 0 on success, -1 if tape is full
 */
static inline int d8_vsb_add_frame(d8_vsb_tape_t* tape, d8_u32 frame_tag,
                                    const d8_u8 data[D8_K_LANES]) {
    if (tape->count >= D8_VSB_MAX_FRAMES) return -1;
    tape->frames[tape->count].frame_tag = frame_tag;
    for (size_t i = 0; i < D8_K_LANES; i++) {
        tape->frames[tape->count].data[i] = data[i];
    }
    tape->count++;
    return 0;
}

/**
 * @brief Clear all frames from the tape
 * 
 * @param tape Pointer to tape
 */
static inline void d8_vsb_clear(d8_vsb_tape_t* tape) {
    tape->count = 0;
}

#ifdef __cplusplus
}
#endif
