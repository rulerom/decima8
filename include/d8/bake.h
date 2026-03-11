/*
 * DECIMA-8 Core - Pure C Implementation
 * Bake API
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Bake view structures (for serialization)
 * ============================================================================ */

typedef struct d8_bake_view {
    /* Tile parameters - static arrays for 4096 tiles */
    d8_i16  thr_lo[D8_K_TILE_COUNT];
    d8_i16  thr_hi[D8_K_TILE_COUNT];
    d8_u16  decay16[D8_K_TILE_COUNT];
    d8_u8   domain_id[D8_K_TILE_COUNT];
    d8_u8   priority[D8_K_TILE_COUNT];
    d8_u16  pattern_id[D8_K_TILE_COUNT];
    d8_u16  reset_on_fire_mask16[D8_K_TILE_COUNT];

    /* Routing flags */
    d8_u8   maskN[D8_K_TILE_COUNT];
    d8_u8   maskE[D8_K_TILE_COUNT];
    d8_u8   maskS[D8_K_TILE_COUNT];
    d8_u8   maskW[D8_K_TILE_COUNT];
    d8_u8   maskNE[D8_K_TILE_COUNT];
    d8_u8   maskSE[D8_K_TILE_COUNT];
    d8_u8   maskSW[D8_K_TILE_COUNT];
    d8_u8   maskNW[D8_K_TILE_COUNT];
    d8_u8   bus_w[D8_K_TILE_COUNT];
    d8_u8   bus_r[D8_K_TILE_COUNT];

    /* Weights (packed: 64 mags + 64 signs per tile) */
    d8_u8   w_mag[D8_K_TILE_COUNT * 64];  /* 64 per tile, nibbles 0..7 */
    d8_u8   w_sign[D8_K_TILE_COUNT * 64]; /* 64 per tile, bits 0/1 */

    /* Topology */
    d8_topology_t topo;

    /* Readout policy */
    d8_readout_policy_t readout;

    /* Metadata */
    d8_u32 tile_field_limit;
    d8_u32 bake_id;
    d8_u32 profile_id;
    d8_u32 bake_flags;

    /* Tile count */
    size_t tile_count;
} d8_bake_view_t;

/* ============================================================================
 * Bake validation
 * ============================================================================ */

/**
 * @brief Validate bake blob
 * @param blob Bake blob data
 * @param blob_size Size of blob
 * @param out_view Optional: output view if validation passes
 * @return Status code
 */
d8_status_t d8_bake_validate(const d8_u8* blob, size_t blob_size, d8_bake_view_t* out_view);

/**
 * @brief Parse bake blob into view
 * @param blob Bake blob data
 * @param blob_size Size of blob
 * @param out_view Output view
 * @return Status code
 */
d8_status_t d8_bake_parse_view(const d8_u8* blob, size_t blob_size, d8_bake_view_t* out_view);

/**
 * @brief Free bake view resources
 * @param view Pointer to view
 */
void d8_bake_free_view(d8_bake_view_t* view);

/* ============================================================================
 * Bake serialization
 * ============================================================================ */

/**
 * @brief Serialize bake view to canonical blob
 * @param view Input view
 * @param out_buf Output buffer (must be large enough)
 * @param out_len Output length
 * @return Status code
 */
d8_status_t d8_bake_serialize_canonical(const d8_bake_view_t* view,
                                         d8_u8* out_buf, size_t* out_len);

/**
 * @brief Estimate size needed for serialized bake
 * @param tile_count Number of tiles
 * @return Estimated size in bytes
 */
size_t d8_bake_estimate_serialized_size(size_t tile_count);

/* ============================================================================
 * CRC32 IEEE
 * ============================================================================ */

/**
 * @brief Calculate CRC32 IEEE (zlib compatible)
 * @param data Data buffer
 * @param length Data length
 * @return CRC32 value
 */
d8_u32 d8_crc32_ieee(const d8_u8* data, size_t length);

#ifdef __cplusplus
}
#endif
