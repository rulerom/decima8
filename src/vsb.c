/*
 * DECIMA-8 Core - Pure C Implementation
 * VSB (Vector Stream Buffer) format implementation
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

#include "d8/vsb.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Little-endian helpers
 * ============================================================================ */

static inline d8_u32 read_le_u32(const d8_u8* p) {
    return (d8_u32)p[0] | ((d8_u32)p[1] << 8) |
           ((d8_u32)p[2] << 16) | ((d8_u32)p[3] << 24);
}

static inline void write_le_u32(d8_u8* p, d8_u32 v) {
    p[0] = (d8_u8)(v & 0xFF);
    p[1] = (d8_u8)((v >> 8) & 0xFF);
    p[2] = (d8_u8)((v >> 16) & 0xFF);
    p[3] = (d8_u8)((v >> 24) & 0xFF);
}

/* ============================================================================
 * VSB Loading
 * ============================================================================ */

int d8_vsb_load_from_memory(const d8_u8* data, size_t size, int tagged,
                             d8_vsb_tape_t* tape) {
    if (!data || !tape) return -1;

    tape->count = 0;

    /* IDE format: 4 bytes per frame (8 channels, 4-bit each) */
    size_t frame_size = tagged ? 8 : 4;  /* tagged: 4-byte tag + 4-byte data */

    if (size < frame_size) {
        return -1;  /* File too small */
    }

    size_t num_frames = size / frame_size;
    if (num_frames > D8_VSB_MAX_FRAMES) {
        num_frames = D8_VSB_MAX_FRAMES;
    }

    for (size_t i = 0; i < num_frames; i++) {
        const d8_u8* frame_ptr = data + (i * frame_size);

        if (tagged) {
            /* Tagged format: 4-byte tag + 4-byte packed data */
            tape->frames[i].frame_tag = read_le_u32(frame_ptr);
            
            /* Unpack 4 bytes to 8 channels (4-bit each) */
            for (int j = 0; j < 4; j++) {
                d8_u8 b = frame_ptr[4 + j];
                tape->frames[i].data[j * 2] = b & 0x0F;           /* Low nibble */
                tape->frames[i].data[j * 2 + 1] = (b >> 4) & 0x0F; /* High nibble */
            }
        } else {
            /* Raw format: 4-byte packed data, tag = index */
            tape->frames[i].frame_tag = (d8_u32)i;
            
            /* Unpack 4 bytes to 8 channels (4-bit each) */
            for (int j = 0; j < 4; j++) {
                d8_u8 b = frame_ptr[j];
                tape->frames[i].data[j * 2] = b & 0x0F;           /* Low nibble */
                tape->frames[i].data[j * 2 + 1] = (b >> 4) & 0x0F; /* High nibble */
            }
        }

        tape->count++;
    }

    return 0;
}

int d8_vsb_load(const char* path, d8_vsb_tape_t* tape) {
    if (!path || !tape) return -1;

    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open VSB file: %s\n", path);
        return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) {
        fprintf(stderr, "Failed to get VSB file size: %s\n", path);
        fclose(f);
        return -1;
    }

    /* IDE format: always 4 bytes per frame (8 channels, 4-bit each) */
    /* No tagged format in IDE - all files are raw 4-byte frames */
    size_t frame_size = 4;
    
    if (file_size % 4 != 0) {
        fprintf(stderr, "Invalid VSB file size: %ld (not multiple of 4)\n", file_size);
        fclose(f);
        return -1;
    }

    tape->count = 0;
    size_t num_frames = (size_t)file_size / frame_size;

    if (num_frames > D8_VSB_MAX_FRAMES) {
        fprintf(stderr, "Warning: VSB file has %zu frames, limiting to %d\n",
                num_frames, D8_VSB_MAX_FRAMES);
        num_frames = D8_VSB_MAX_FRAMES;
    }

    d8_u8 buffer[4];
    for (size_t i = 0; i < num_frames; i++) {
        if (fread(buffer, 1, frame_size, f) != frame_size) {
            fprintf(stderr, "Failed to read frame %zu from VSB file\n", i);
            break;
        }

        /* Frame tag = index (IDE doesn't use tags in file) */
        tape->frames[i].frame_tag = (d8_u32)i;
        
        /* Unpack 4 bytes to 8 channels (4-bit each) */
        for (int j = 0; j < 4; j++) {
            d8_u8 b = buffer[j];
            tape->frames[i].data[j * 2] = b & 0x0F;           /* Low nibble */
            tape->frames[i].data[j * 2 + 1] = (b >> 4) & 0x0F; /* High nibble */
        }

        tape->count++;
    }

    fclose(f);
    printf("Loaded %zu frames from %s\n", tape->count, path);
    return 0;
}

int d8_vsb_save(const char* path, const d8_vsb_tape_t* tape) {
    if (!path || !tape) return -1;

    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to create VSB file: %s\n", path);
        return -1;
    }

    /* Save in IDE format: 4 bytes per frame (packed 4-bit channels) */
    for (size_t i = 0; i < tape->count; i++) {
        d8_u8 buffer[4];
        /* Pack 8 channels (4-bit each) into 4 bytes */
        for (int j = 0; j < 4; j++) {
            d8_u8 low = tape->frames[i].data[j * 2] & 0x0F;
            d8_u8 high = (tape->frames[i].data[j * 2 + 1] & 0x0F) << 4;
            buffer[j] = low | high;
        }

        if (fwrite(buffer, 1, 4, f) != 4) {
            fprintf(stderr, "Failed to write frame %zu to VSB file\n", i);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    printf("Saved %zu frames to %s\n", tape->count, path);
    return 0;
}
