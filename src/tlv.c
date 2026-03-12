/*
 * DECIMA-8 Core - Pure C Implementation
 * TLV Parser Implementation
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/bake.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * TLV Header structure
 * ============================================================================ */

typedef struct d8_tlv_header {
    d8_u16 type;
    d8_u16 tflags;
    d8_u32 len;
} d8_tlv_header_t;

/* ============================================================================
 * Helper functions
 * ============================================================================ */

static d8_u16 read_u16_le(const d8_u8* p) {
    return (d8_u16)(p[0] | (p[1] << 8));
}

static d8_u32 read_u32_le(const d8_u8* p) {
    return (d8_u32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static d8_status_t parse_tlv_header(const d8_u8* data, size_t remaining,
                                     d8_tlv_header_t* out_header, size_t* out_consumed) {
    if (remaining < 8) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, 0, "TLV header too small");
    }

    out_header->type = read_u16_le(data);
    out_header->tflags = read_u16_le(data + 2);
    out_header->len = read_u32_le(data + 4);

    *out_consumed = 8;

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

/* ============================================================================
 * TLV Parsers
 * ============================================================================ */

static d8_status_t parse_topology(const d8_u8* data, size_t len, d8_topology_t* out) {
    if (len != 16) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, (d8_u32)len, "TOPOLOGY len != 16");
    }

    out->tile_count = read_u32_le(data);
    out->tile_w = read_u16_le(data + 4);
    out->tile_h = read_u16_le(data + 6);
    out->lanes = data[8];
    out->domains = data[9];
    out->reserved = read_u16_le(data + 10);  /* reserved (bytes 10-11) */
    out->reserved2 = read_u32_le(data + 12);  /* reserved2/tile_field_limit (bytes 12-15) */

    /* Validate */
    if (out->lanes != D8_K_LANES || out->domains != D8_K_DOMAINS) {
        return D8_STATUS_MAKE(D8_STATUS_TOPOLOGY_MISMATCH, 0, "Bad lanes/domains");
    }

    /* Validate reserved (bytes 10-11) must be 0 */
    if (out->reserved != 0) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_RESERVED_NON_ZERO, 0, "TOPOLOGY reserved must be 0");
    }

    /* reserved2 (tile_field_limit) is allowed to be non-zero */

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

static d8_status_t parse_tile_params_v2(const d8_u8* data, size_t len,
                                         d8_i16* thr_lo, d8_i16* thr_hi,
                                         d8_u16* decay16, d8_u8* domain_id,
                                         d8_u8* priority, d8_u16* pattern_id,
                                         d8_u16* reset_on_fire, size_t tile_count) {
    const size_t expected_len = tile_count * 13;
    if (len != expected_len) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, (d8_u32)len, "TILE_PARAMS bad len");
    }

    for (size_t t = 0; t < tile_count; t++) {
        const d8_u8* p = data + t * 13;

        thr_lo[t] = (d8_i16)read_u16_le(p);
        thr_hi[t] = (d8_i16)read_u16_le(p + 2);
        decay16[t] = read_u16_le(p + 4);
        domain_id[t] = p[6] & 0x0F;  /* low nibble only */
        priority[t] = p[7];
        pattern_id[t] = read_u16_le(p + 8);
        reset_on_fire[t] = 0;  /* Will be set from separate TLV */

        /* Validate */
        if (thr_lo[t] > thr_hi[t]) {
            return D8_STATUS_MAKE(D8_STATUS_BAKE_RESERVED_NON_ZERO, (d8_u32)t, "thr_lo > thr_hi");
        }
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

static d8_status_t parse_routing_flags(const d8_u8* data, size_t len,
                                        d8_u8* maskN, d8_u8* maskE, d8_u8* maskS, d8_u8* maskW,
                                        d8_u8* maskNE, d8_u8* maskSE, d8_u8* maskSW, d8_u8* maskNW,
                                        d8_u8* bus_w, d8_u8* bus_r, size_t tile_count) {
    const size_t expected_len = tile_count * 2;
    if (len != expected_len) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, (d8_u32)len, "ROUTING_FLAGS bad len");
    }

    for (size_t t = 0; t < tile_count; t++) {
        d8_u16 flags = read_u16_le(data + t * 2);

        maskN[t] = (flags & 0x01) ? 1 : 0;
        maskE[t] = (flags & 0x02) ? 1 : 0;
        maskS[t] = (flags & 0x04) ? 1 : 0;
        maskW[t] = (flags & 0x08) ? 1 : 0;
        maskNE[t] = (flags & 0x10) ? 1 : 0;
        maskSE[t] = (flags & 0x20) ? 1 : 0;
        maskSW[t] = (flags & 0x40) ? 1 : 0;
        maskNW[t] = (flags & 0x80) ? 1 : 0;
        bus_w[t] = (flags & 0x0200) ? 1 : 0;
        bus_r[t] = (flags & 0x0100) ? 1 : 0;

        /* Validate reserved bits */
        if (flags & 0xFC00) {
            return D8_STATUS_MAKE(D8_STATUS_BAKE_RESERVED_NON_ZERO, flags, "Reserved bits set");
        }
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

static d8_status_t parse_weights_packed(const d8_u8* data, size_t len,
                                         d8_u8* w_mag, d8_u8* w_sign, size_t tile_count) {
    const size_t expected_len = tile_count * 40;  /* 32 bytes mag + 8 bytes sign */
    if (len != expected_len) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, (d8_u32)len, "WEIGHTS bad len");
    }

    for (size_t t = 0; t < tile_count; t++) {
        const d8_u8* tile_data = data + t * 40;

        /* Magnitudes: 64 nibbles in 32 bytes */
        for (size_t i = 0; i < 32; i++) {
            d8_u8 byte = tile_data[i];
            w_mag[t * 64 + i * 2 + 0] = byte & 0x0F;
            w_mag[t * 64 + i * 2 + 1] = (byte >> 4) & 0x0F;
        }

        /* Signs: 64 bits in 8 bytes, LSB-first */
        for (size_t i = 0; i < 8; i++) {
            d8_u8 byte = tile_data[32 + i];
            for (int bit = 0; bit < 8; bit++) {
                w_sign[t * 64 + i * 8 + bit] = (byte >> bit) & 1;
            }
        }
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

static d8_status_t parse_reset_on_fire(const d8_u8* data, size_t len,
                                        d8_u16* reset_on_fire, size_t tile_count) {
    const size_t expected_len = tile_count * 2;
    if (len != expected_len) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, (d8_u32)len, "RESET_ON_FIRE bad len");
    }

    for (size_t t = 0; t < tile_count; t++) {
        reset_on_fire[t] = read_u16_le(data + t * 2);
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

static d8_status_t parse_readout_policy(const d8_u8* data, size_t len, d8_readout_policy_t* out) {
    if (len != 12) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, (d8_u32)len, "READOUT len != 12");
    }

    out->mode = data[0];
    out->reserved0 = data[1];
    out->winner_domain_mask = read_u16_le(data + 2);
    out->settle_ns = read_u16_le(data + 4);
    out->reserved1 = read_u16_le(data + 6);
    out->reserved2 = read_u32_le(data + 8);

    /* Validate */
    if (out->mode > 1) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_RESERVED_NON_ZERO, out->mode, "Bad readout mode");
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

/* ============================================================================
 * Main parse function
 * ============================================================================ */

d8_status_t d8_bake_parse_view(const d8_u8* blob, size_t blob_size, d8_bake_view_t* out_view) {
    if (!blob || !out_view) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "NULL pointer");
    }

    if (blob_size < 28) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_LEN, (d8_u32)blob_size, "Blob too small");
    }

    /* Parse header */
    d8_bake_header_t header;
    memcpy(header.magic, blob, 4);
    header.ver_major = read_u16_le(blob + 4);
    header.ver_minor = read_u16_le(blob + 6);
    header.flags = read_u32_le(blob + 8);
    header.total_len = read_u32_le(blob + 12);
    header.bake_id = read_u32_le(blob + 16);
    header.profile_id = read_u32_le(blob + 20);
    header.reserved0 = read_u32_le(blob + 24);

    /* Validate magic */
    if (header.magic[0] != 'D' || header.magic[1] != '8' ||
        header.magic[2] != 'B' || header.magic[3] != 'K') {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_MAGIC, 0, "Bad magic");
    }

    /* Validate version */
    if (header.ver_major != 2) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_VERSION, header.ver_major, "Bad version");
    }

    /* Validate total length - total_len must match actual blob size */
    if (header.total_len != blob_size) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_LEN_TOTAL, header.total_len, "Bad total_len");
    }

    /* Validate reserved */
    if (header.reserved0 != 0) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_RESERVED_NON_ZERO, header.reserved0, "Header reserved != 0");
    }

    /* Initialize view */
    memset(out_view, 0, sizeof(d8_bake_view_t));
    out_view->bake_id = header.bake_id;
    out_view->profile_id = header.profile_id;
    out_view->bake_flags = header.flags;

    /* Parse TLV records */
    const d8_u8* tlv_data = blob + 28;
    size_t tlv_remaining = header.total_len - 28;  /* TLVs start after 28-byte header */

    bool has_topology = false;
    bool has_tile_params = false;
    bool has_routing = false;
    bool has_weights = false;
    bool has_crc = false;

    while (tlv_remaining > 0) {
        d8_tlv_header_t tlv_hdr;
        size_t consumed;

        d8_status_t st = parse_tlv_header(tlv_data, tlv_remaining, &tlv_hdr, &consumed);
        if (!d8_status_is_ok(&st)) {
            return st;
        }

        tlv_data += consumed;
        tlv_remaining -= consumed;

        /* Validate TLV length */
        if (tlv_hdr.len > tlv_remaining) {
            return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, tlv_hdr.len, "TLV len overflow");
        }

        /* Parse by type */
        switch (tlv_hdr.type) {
            case D8_TLV_TOPOLOGY: {
                st = parse_topology(tlv_data, tlv_hdr.len, &out_view->topo);
                if (!d8_status_is_ok(&st)) return st;
                out_view->tile_count = out_view->topo.tile_count;
                has_topology = true;
                break;
            }

            case D8_TLV_TILE_PARAMS_V2: {
                if (!has_topology) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, 0, "TOPOLOGY before TILE_PARAMS");
                }
                st = parse_tile_params_v2(tlv_data, tlv_hdr.len,
                                          out_view->thr_lo, out_view->thr_hi,
                                          out_view->decay16, out_view->domain_id,
                                          out_view->priority, out_view->pattern_id,
                                          out_view->reset_on_fire_mask16,
                                          out_view->tile_count);
                if (!d8_status_is_ok(&st)) return st;
                has_tile_params = true;
                break;
            }

            case D8_TLV_TILE_ROUTING_FLAGS16: {
                if (!has_topology) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, 0, "TOPOLOGY before ROUTING");
                }
                st = parse_routing_flags(tlv_data, tlv_hdr.len,
                                         out_view->maskN, out_view->maskE, out_view->maskS, out_view->maskW,
                                         out_view->maskNE, out_view->maskSE, out_view->maskSW, out_view->maskNW,
                                         out_view->bus_w, out_view->bus_r,
                                         out_view->tile_count);
                if (!d8_status_is_ok(&st)) return st;
                has_routing = true;
                break;
            }

            case D8_TLV_TILE_WEIGHTS_PACKED: {
                if (!has_topology) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, 0, "TOPOLOGY before WEIGHTS");
                }
                st = parse_weights_packed(tlv_data, tlv_hdr.len,
                                          out_view->w_mag, out_view->w_sign,
                                          out_view->tile_count);
                if (!d8_status_is_ok(&st)) return st;
                has_weights = true;
                break;
            }

            case D8_TLV_RESET_ON_FIRE_MASK16: {
                if (!has_topology) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, 0, "TOPOLOGY before RESET_ON_FIRE");
                }
                st = parse_reset_on_fire(tlv_data, tlv_hdr.len,
                                         out_view->reset_on_fire_mask16,
                                         out_view->tile_count);
                if (!d8_status_is_ok(&st)) return st;
                break;
            }

            case D8_TLV_READOUT_POLICY: {
                st = parse_readout_policy(tlv_data, tlv_hdr.len, &out_view->readout);
                if (!d8_status_is_ok(&st)) return st;
                break;
            }

            case D8_TLV_TILE_FIELD_LIMIT: {
                if (tlv_hdr.len != 4) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, tlv_hdr.len, "TILE_FIELD_LIMIT len != 4");
                }
                /* Read tile_field_limit and store in view for later restoration */
                out_view->tile_field_limit = read_u32_le(tlv_data);
                break;
            }

            case D8_TLV_CRC32: {
                if (tlv_hdr.len != 4) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_BAD_TLV_LEN, tlv_hdr.len, "CRC32 len != 4");
                }

                d8_u32 stored_crc = read_u32_le(tlv_data);
                d8_u32 computed_crc = d8_crc32_ieee(blob, blob_size - 12);  /* Exclude CRC TLV (8 header + 4 value) */

                if (stored_crc != computed_crc) {
                    return D8_STATUS_MAKE(D8_STATUS_BAKE_CRC_FAIL, stored_crc, "CRC mismatch");
                }

                has_crc = true;
                break;
            }

            default:
                /* Unknown TLV - skip */
                break;
        }

        tlv_data += tlv_hdr.len;
        tlv_remaining -= tlv_hdr.len;

        /* Align to 4 bytes */
        size_t padding = (4 - (tlv_hdr.len % 4)) % 4;
        tlv_data += padding;
        tlv_remaining -= padding;
    }

    /* Validate required TLVs */
    if (!has_topology) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, D8_TLV_TOPOLOGY, "Missing TOPOLOGY");
    }
    if (!has_tile_params) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, D8_TLV_TILE_PARAMS_V2, "Missing TILE_PARAMS");
    }
    if (!has_routing) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, D8_TLV_TILE_ROUTING_FLAGS16, "Missing ROUTING");
    }
    if (!has_weights) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, D8_TLV_TILE_WEIGHTS_PACKED, "Missing WEIGHTS");
    }
    if (!has_crc) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_MISSING_TLV, D8_TLV_CRC32, "Missing CRC32");
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_status_t d8_bake_validate(const d8_u8* blob, size_t blob_size, d8_bake_view_t* out_view) {
    if (!out_view) {
        /* Validation only, no output */
        d8_bake_view_t tmp_view;
        return d8_bake_parse_view(blob, blob_size, &tmp_view);
    }
    return d8_bake_parse_view(blob, blob_size, out_view);
}

void d8_bake_free_view(d8_bake_view_t* view) {
    if (!view) return;

    /* Free dynamically allocated memory if any */
    /* Currently all arrays are embedded, nothing to free */
}

size_t d8_bake_estimate_serialized_size(size_t tile_count) {
    /* 
     * Estimate bake blob size with padding alignment
     * Each TLV is padded to 4-byte boundary
     */
    size_t header_size = 28;
    
    size_t topo_raw = 8 + 16;
    size_t topo_size = (topo_raw + 3) & ~3;  /* 24, already aligned */
    
    size_t params_raw = 8 + (tile_count * 13);
    size_t params_size = (params_raw + 3) & ~3;
    
    size_t routing_raw = 8 + (tile_count * 2);
    size_t routing_size = (routing_raw + 3) & ~3;
    
    size_t weights_raw = 8 + (tile_count * 40);
    size_t weights_size = (weights_raw + 3) & ~3;
    
    size_t reset_raw = 8 + (tile_count * 2);
    size_t reset_size = (reset_raw + 3) & ~3;
    
    size_t readout_raw = 8 + 12;
    size_t readout_size = (readout_raw + 3) & ~3;  /* 20, already aligned */

    size_t field_limit_raw = 8 + 4;
    size_t field_limit_size = (field_limit_raw + 3) & ~3;  /* 12, already aligned */

    size_t crc_raw = 8 + 4;
    size_t crc_size = (crc_raw + 3) & ~3;  /* 12, already aligned */

    return header_size + topo_size + params_size + routing_size +
           weights_size + reset_size + readout_size + field_limit_size + crc_size;
}

#ifdef __cplusplus
}
#endif
