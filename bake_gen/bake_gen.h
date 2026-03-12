/*
 * DECIMA-8 Bake Generator - Pure C Implementation
 *
 * All rights belong to the ORDEN (c) 2026
 */

#ifndef D8_BAKE_GEN_H
#define D8_BAKE_GEN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants (with ifndef guards to avoid redefinition warnings)
 * ============================================================================ */

#ifndef D8_K_LANES
#define D8_K_LANES          8U
#endif
#ifndef D8_K_DOMAINS
#define D8_K_DOMAINS        16U
#endif
#ifndef D8_K_EXPECTED_W
#define D8_K_EXPECTED_W     128U
#endif
#ifndef D8_K_EXPECTED_H
#define D8_K_EXPECTED_H     32U
#endif
#ifndef D8_K_TILE_COUNT
#define D8_K_TILE_COUNT     (D8_K_EXPECTED_W * D8_K_EXPECTED_H)
#endif

#ifndef D8_BAKE_VER_MAJOR
#define D8_BAKE_VER_MAJOR   2U
#endif
#ifndef D8_BAKE_VER_MINOR
#define D8_BAKE_VER_MINOR   0U
#endif

/* TLV Types (v0.2) */
#ifndef D8_TLV_TOPOLOGY
#define D8_TLV_TOPOLOGY             0x0100U
#endif
#ifndef D8_TLV_TILE_PARAMS_V2
#define D8_TLV_TILE_PARAMS_V2       0x0121U
#endif
#ifndef D8_TLV_TILE_ROUTING_FLAGS16
#define D8_TLV_TILE_ROUTING_FLAGS16 0x0131U
#endif
#ifndef D8_TLV_READOUT_POLICY
#define D8_TLV_READOUT_POLICY       0x0140U
#endif
#ifndef D8_TLV_RESET_ON_FIRE_MASK16
#define D8_TLV_RESET_ON_FIRE_MASK16 0x0150U
#endif
#ifndef D8_TLV_TILE_WEIGHTS_PACKED
#define D8_TLV_TILE_WEIGHTS_PACKED  0x0160U
#endif
#ifndef D8_TLV_TILE_FIELD_LIMIT
#define D8_TLV_TILE_FIELD_LIMIT     0x0170U
#endif
#ifndef D8_TLV_CRC32
#define D8_TLV_CRC32                0xFFFEU
#endif

/* ============================================================================
 * Bake Generator API
 * ============================================================================ */

/* Tile params sizes */
#define D8_SZ_TILE_PARAMS_V2        13U
#define D8_SZ_TILE_ROUTING_FLAGS16  2U
#define D8_SZ_TILE_WEIGHTS_PACKED   40U
#define D8_SZ_RESET_ON_FIRE_MASK16  2U

/**
 * @brief Generate a minimal test bake blob
 * @param out_buffer Output buffer (must be at least 240000 bytes)
 * @param out_size Pointer to store actual size
 * @return 0 on success, -1 on error
 */
int d8_bake_gen_test(uint8_t* out_buffer, size_t* out_size);

/**
 * @brief Generate a bake blob with custom parameters
 * @param out_buffer Output buffer (must be at least 240000 bytes)
 * @param out_size Pointer to store actual size
 * @param tile_count Number of tiles (1..4096)
 * @param default_thr_lo Default thr_lo for all tiles
 * @param default_thr_hi Default thr_hi for all tiles
 * @param default_decay16 Default decay16 for all tiles
 * @return 0 on success, -1 on error
 */
int d8_bake_gen_custom(
    uint8_t* out_buffer,
    size_t* out_size,
    uint32_t tile_count,
    int16_t default_thr_lo,
    int16_t default_thr_hi,
    uint16_t default_decay16
);

/**
 * @brief Calculate estimated bake blob size
 * @param tile_count Number of tiles
 * @return Estimated size in bytes
 */
size_t d8_bake_gen_estimate_size(uint32_t tile_count);

#ifdef __cplusplus
}
#endif

#endif /* D8_BAKE_GEN_H */
