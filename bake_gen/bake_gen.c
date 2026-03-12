/*
 * DECIMA-8 Bake Generator - Pure C Implementation
 * 
 * All rights belong to the ORDEN (c) 2026
 */

#include "bake_gen.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void write_le_u16(uint8_t* dst, uint16_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
}

static void write_le_u32(uint8_t* dst, uint32_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
}

static uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    static const uint32_t table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
        0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
        0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
        0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
        0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
        0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
        0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
        0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
        0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
        0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
        0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
        0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
        0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
        0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
        0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
        0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
        0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
        0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
        0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
        0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
        0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
        0xBAD03605, 0xCDD706B3, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
    };
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

size_t d8_bake_gen_estimate_size(uint32_t tile_count) {
    size_t header_size = 28;
    size_t topology_size = 8 + 16;  /* 24, already aligned */
    size_t tile_params_raw = 8 + (tile_count * D8_SZ_TILE_PARAMS_V2);
    size_t tile_params_size = (tile_params_raw + 3) & ~3;  /* pad to 4 */
    size_t routing_raw = 8 + (tile_count * D8_SZ_TILE_ROUTING_FLAGS16);
    size_t routing_size = (routing_raw + 3) & ~3;  /* pad to 4 */
    size_t weights_raw = 8 + (tile_count * D8_SZ_TILE_WEIGHTS_PACKED);
    size_t weights_size = (weights_raw + 3) & ~3;  /* pad to 4 */
    size_t reset_raw = 8 + (tile_count * D8_SZ_RESET_ON_FIRE_MASK16);
    size_t reset_on_fire_size = (reset_raw + 3) & ~3;  /* pad to 4 */
    size_t readout_size = 8 + 12;  /* 20, already aligned */
    size_t field_limit_size = 8 + 4;  /* 12, already aligned */
    size_t crc_size = 8 + 4;  /* 12, already aligned */

    return header_size + topology_size + tile_params_size +
           routing_size + weights_size + reset_on_fire_size +
           readout_size + field_limit_size + crc_size;
}

int d8_bake_gen_test(uint8_t* out_buffer, size_t* out_size) {
    return d8_bake_gen_custom(out_buffer, out_size, D8_K_TILE_COUNT, 0, 0, 0);
}

int d8_bake_gen_custom(
    uint8_t* out_buffer,
    size_t* out_size,
    uint32_t tile_count,
    int16_t default_thr_lo,
    int16_t default_thr_hi,
    uint16_t default_decay16
) {
    if (!out_buffer || !out_size || tile_count == 0 || tile_count > D8_K_TILE_COUNT) {
        return -1;
    }
    
    size_t total_size = d8_bake_gen_estimate_size(tile_count);
    memset(out_buffer, 0, total_size);
    
    size_t offset = 0;
    
    /* ========== Header (28 bytes) ========== */
    out_buffer[offset++] = 'D';
    out_buffer[offset++] = '8';
    out_buffer[offset++] = 'B';
    out_buffer[offset++] = 'K';
    write_le_u16(out_buffer + offset, D8_BAKE_VER_MAJOR); offset += 2;
    write_le_u16(out_buffer + offset, D8_BAKE_VER_MINOR); offset += 2;
    write_le_u32(out_buffer + offset, 0); offset += 4;  /* flags */
    
    size_t total_len_offset = offset;
    write_le_u32(out_buffer + offset, 0); offset += 4;  /* total_len placeholder */
    
    write_le_u32(out_buffer + offset, 1); offset += 4;  /* bake_id */
    write_le_u32(out_buffer + offset, 1); offset += 4;  /* profile_id */
    write_le_u32(out_buffer + offset, 0); offset += 4;  /* reserved0 */

    /* ========== TLV_TOPOLOGY (24 bytes) ========== */
    write_le_u16(out_buffer + offset, 0x0100); offset += 2;  /* type = TOPOLOGY */
    write_le_u16(out_buffer + offset, 0); offset += 2;  /* tflags */
    write_le_u32(out_buffer + offset, 16); offset += 4;  /* len */

    write_le_u32(out_buffer + offset, tile_count); offset += 4;
    write_le_u16(out_buffer + offset, D8_K_EXPECTED_W); offset += 2;
    write_le_u16(out_buffer + offset, D8_K_EXPECTED_H); offset += 2;
    out_buffer[offset++] = D8_K_LANES;
    out_buffer[offset++] = D8_K_DOMAINS;
    write_le_u16(out_buffer + offset, 0); offset += 2;  /* reserved */
    write_le_u32(out_buffer + offset, tile_count); offset += 4;  /* reserved2 = tile_field_limit */
    
    /* ========== TLV_TILE_PARAMS_V2 ========== */
    size_t params_len = tile_count * D8_SZ_TILE_PARAMS_V2;
    write_le_u16(out_buffer + offset, D8_TLV_TILE_PARAMS_V2); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, (uint32_t)params_len); offset += 4;
    
    for (uint32_t t = 0; t < tile_count; t++) {
        write_le_u16(out_buffer + offset, (uint16_t)default_thr_lo); offset += 2;
        write_le_u16(out_buffer + offset, (uint16_t)default_thr_hi); offset += 2;
        write_le_u16(out_buffer + offset, default_decay16); offset += 2;
        out_buffer[offset++] = 0;  /* domain_id */
        out_buffer[offset++] = 0;  /* priority */
        write_le_u16(out_buffer + offset, 0); offset += 2;  /* pattern_id */
        out_buffer[offset++] = 0;  /* flags */
        write_le_u16(out_buffer + offset, 0); offset += 2;  /* reserved */
    }
    
    /* Pad to 4 bytes */
    while (offset % 4 != 0) {
        out_buffer[offset++] = 0;
    }
    
    /* ========== TLV_TILE_ROUTING_FLAGS16 ========== */
    size_t routing_len = tile_count * D8_SZ_TILE_ROUTING_FLAGS16;
    write_le_u16(out_buffer + offset, D8_TLV_TILE_ROUTING_FLAGS16); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, (uint32_t)routing_len); offset += 4;
    
    for (uint32_t t = 0; t < tile_count; t++) {
        write_le_u16(out_buffer + offset, 0); offset += 2;  /* routing_flags16 */
    }
    
    /* Pad to 4 bytes */
    while (offset % 4 != 0) {
        out_buffer[offset++] = 0;
    }

    /* ========== TLV_READOUT_POLICY (20 bytes) ========== */
    write_le_u16(out_buffer + offset, D8_TLV_READOUT_POLICY); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, 12); offset += 4;

    out_buffer[offset++] = 0;  /* mode = R0_RAW_BUS */
    out_buffer[offset++] = 0;  /* reserved0 */
    write_le_u16(out_buffer + offset, 0); offset += 2;  /* winner_domain_mask */
    write_le_u16(out_buffer + offset, 0); offset += 2;  /* settle_ns */
    write_le_u16(out_buffer + offset, 0); offset += 2;  /* reserved1 */
    write_le_u32(out_buffer + offset, 0); offset += 4;  /* reserved2 */

    /* Pad to 4 bytes */
    while (offset % 4 != 0) {
        out_buffer[offset++] = 0;
    }

    /* ========== TLV_RESET_ON_FIRE_MASK16 ========== */
    size_t reset_len = tile_count * D8_SZ_RESET_ON_FIRE_MASK16;
    write_le_u16(out_buffer + offset, D8_TLV_RESET_ON_FIRE_MASK16); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, (uint32_t)reset_len); offset += 4;

    for (uint32_t t = 0; t < tile_count; t++) {
        write_le_u16(out_buffer + offset, 0); offset += 2;  /* reset_mask16 */
    }

    /* Pad to 4 bytes */
    while (offset % 4 != 0) {
        out_buffer[offset++] = 0;
    }

    /* ========== TLV_TILE_WEIGHTS_PACKED ========== */
    size_t weights_len = tile_count * D8_SZ_TILE_WEIGHTS_PACKED;
    write_le_u16(out_buffer + offset, D8_TLV_TILE_WEIGHTS_PACKED); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, (uint32_t)weights_len); offset += 4;

    for (uint32_t t = 0; t < tile_count; t++) {
        /* 64 nibbles (magnitudes) packed into 32 bytes */
        for (size_t i = 0; i < 32; i++) {
            out_buffer[offset++] = 0;
        }
        /* 64 bits (signs) packed into 8 bytes */
        for (size_t i = 0; i < 8; i++) {
            out_buffer[offset++] = 0;
        }
    }

    /* Pad to 4 bytes */
    while (offset % 4 != 0) {
        out_buffer[offset++] = 0;
    }

    /* ========== TLV_TILE_FIELD_LIMIT ========== */
    write_le_u16(out_buffer + offset, D8_TLV_TILE_FIELD_LIMIT); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, 4); offset += 4;
    write_le_u32(out_buffer + offset, tile_count); offset += 4;  /* tile_field_limit */

    /* Write total_len BEFORE CRC (total_len is part of CRC calculation) */
    /* total_len = full blob size including CRC TLV (12 bytes) */
    size_t size_before_crc = offset;
    uint32_t total_len_value = (uint32_t)(size_before_crc + 12);
    write_le_u32(out_buffer + total_len_offset, total_len_value);

    /* CRC is calculated over header + TLVs (including total_len, excluding CRC TLV) */
    uint32_t crc = crc32_ieee(out_buffer, offset);

    /* ========== TLV_CRC32 (12 bytes) ========== */
    write_le_u16(out_buffer + offset, D8_TLV_CRC32); offset += 2;
    write_le_u16(out_buffer + offset, 0); offset += 2;
    write_le_u32(out_buffer + offset, 4); offset += 4;
    write_le_u32(out_buffer + offset, crc); offset += 4;

    *out_size = offset;
    return 0;
}
