/*
 * DECIMA-8 Core - Pure C Implementation
 * Swarm API
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Swarm creation/destruction
 * ============================================================================ */

/**
 * @brief Create a new swarm instance
 * @return Pointer to swarm, or NULL on failure
 */
d8_swarm_t* d8_swarm_create(void);

/**
 * @brief Destroy swarm instance
 * @param swarm Pointer to swarm
 */
void d8_swarm_destroy(d8_swarm_t* swarm);

/**
 * @brief Reset swarm to initial state
 * @param swarm Pointer to swarm
 */
void d8_swarm_full_reset(d8_swarm_t* swarm);

/* ============================================================================
 * Bake operations
 * ============================================================================ */

/**
 * @brief Apply bake blob to swarm
 * @param swarm Pointer to swarm
 * @param blob Bake blob data
 * @param blob_size Size of blob in bytes
 * @return Status code
 */
d8_status_t d8_swarm_ev_bake(d8_swarm_t* swarm, const d8_u8* blob, size_t blob_size);

/**
 * @brief Check if bake is applied
 * @param swarm Pointer to swarm
 * @return true if bake is applied
 */
bool d8_swarm_is_baked(const d8_swarm_t* swarm);

/* ============================================================================
 * Flash operations
 * ============================================================================ */

/**
 * @brief Execute one flash cycle (READ → WRITE)
 * @param swarm Pointer to swarm
 * @param tag Frame tag
 * @param vsb_ingress16 Input vector (8 lanes, Level16 0..15)
 * @return Flash result with readout and flags
 */
d8_flash_result_t d8_swarm_ev_flash(d8_swarm_t* swarm, d8_u32 tag, const d8_u8 vsb_ingress16[D8_K_LANES]);

/**
 * @brief Set VSB ingress and execute flash
 * @param swarm Pointer to swarm
 * @param tag Frame tag
 * @param vsb_ingress16 Input vector (8 lanes)
 * @return Flash result
 */
d8_flash_result_t d8_swarm_set_vsb_and_flash(d8_swarm_t* swarm, d8_u32 tag, const d8_u8 vsb_ingress16[D8_K_LANES]);

/* ============================================================================
 * Reset domain operations
 * ============================================================================ */

/**
 * @brief Reset domains by mask
 * @param swarm Pointer to swarm
 * @param mask16 Domain mask (bit per domain)
 * @return Status code
 */
d8_status_t d8_swarm_ev_reset_domain(d8_swarm_t* swarm, d8_u16 mask16);

/* ============================================================================
 * Tile state queries (for IDE/telemetry)
 * ============================================================================ */

/**
 * @brief Get snapshot pointer (read-only)
 * @param swarm Pointer to swarm
 * @return Pointer to snapshot, or NULL
 */
const d8_view_snapshot_t* d8_swarm_get_snapshot(const d8_swarm_t* swarm);

/**
 * @brief Set snapshot bake_id_active
 * @param swarm Pointer to swarm
 * @param bake_id Bake ID
 * @param profile_id Profile ID
 */
void d8_swarm_set_snapshot_bake_id(d8_swarm_t* swarm, d8_u32 bake_id, d8_u32 profile_id);

/**
 * @brief Update shared_buffer bake_id_active
 * @param swarm Pointer to swarm
 * @param bake_id Bake ID
 * @param profile_id Profile ID
 */
void d8_swarm_update_shared_buffer_bake_id(d8_swarm_t* swarm, d8_u32 bake_id, d8_u32 profile_id);

/**
 * @brief Set swarm bake_id and profile_id
 * @param swarm Pointer to swarm
 * @param bake_id Bake ID
 * @param profile_id Profile ID
 */
void d8_swarm_set_bake_id(d8_swarm_t* swarm, d8_u32 bake_id, d8_u32 profile_id);

/**
 * @brief Get active tile count
 * @param swarm Pointer to swarm
 * @return Number of active tiles
 */
size_t d8_swarm_get_active_tile_count(const d8_swarm_t* swarm);

/**
 * @brief Check if tile is locked
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID (0..4095)
 * @return true if locked
 */
bool d8_swarm_get_tile_locked(const d8_swarm_t* swarm, size_t tile_id);

/**
 * @brief Get tile thr_cur (accumulator)
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @return thr_cur value (i16)
 */
d8_i16 d8_swarm_get_tile_thr_cur(const d8_swarm_t* swarm, size_t tile_id);

/**
 * @brief Get tile pattern_id
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @return pattern_id
 */
d8_u16 d8_swarm_get_tile_pattern_id(const d8_swarm_t* swarm, size_t tile_id);

/**
 * @brief Get tile routing masks
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @return Routing masks structure
 */
d8_tile_routing_masks_t d8_swarm_get_tile_routing_masks(const d8_swarm_t* swarm, size_t tile_id);

/**
 * @brief Get tile children (for hierarchy visualization)
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @param out_children Output array (caller must free)
 * @param out_count Number of children
 */
void d8_swarm_get_tile_children(const d8_swarm_t* swarm, size_t tile_id, 
                                 size_t** out_children, size_t* out_count);

/**
 * @brief Free children array
 * @param children Array to free
 */
void d8_swarm_free_tile_children(size_t* children);

/* ============================================================================
 * Tile state modification (for IDE)
 * ============================================================================ */

/**
 * @brief Set tile locked state manually
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @param locked true to lock, false to unlock
 * @return Status code
 */
d8_status_t d8_swarm_set_tile_locked(d8_swarm_t* swarm, size_t tile_id, bool locked);

/**
 * @brief Set tile parameters (thr_lo, thr_hi, decay, domain, priority)
 */
d8_status_t d8_swarm_set_tile_params(d8_swarm_t* swarm, size_t tile_id,
                                      d8_i16 thr_lo, d8_i16 thr_hi,
                                      d8_u16 decay16, d8_u8 domain_id,
                                      d8_u8 priority, d8_u16 pattern_id);

/**
 * @brief Set tile routing masks
 */
d8_status_t d8_swarm_set_tile_routing_masks(d8_swarm_t* swarm, size_t tile_id,
                                             d8_u8 maskN, d8_u8 maskE, d8_u8 maskS, d8_u8 maskW,
                                             d8_u8 maskNE, d8_u8 maskSE, d8_u8 maskSW, d8_u8 maskNW,
                                             d8_u8 bus_w, d8_u8 bus_r);

/**
 * @brief Set tile weight sign
 * @param weight_idx 0..63 (8x8 matrix, row-major)
 */
d8_status_t d8_swarm_set_tile_weight_sign(d8_swarm_t* swarm, size_t tile_id, 
                                           size_t weight_idx, bool sign);

/**
 * @brief Set tile weight magnitude
 * @param weight_idx 0..63
 * @param mag 0..15
 */
d8_status_t d8_swarm_set_tile_weight_mag(d8_swarm_t* swarm, size_t tile_id,
                                          size_t weight_idx, d8_u8 mag);

/**
 * @brief Set tile reset_on_fire_mask16
 */
d8_status_t d8_swarm_set_tile_reset_on_fire_mask(d8_swarm_t* swarm, size_t tile_id,
                                                  d8_u16 mask16);

/* ============================================================================
 * Serialization
 * ============================================================================ */

/**
 * @brief Serialize current bake to blob
 * @param swarm Pointer to swarm
 * @param out_blob Output buffer (caller must free)
 * @param out_size Size of output blob
 * @return Status code
 */
d8_status_t d8_swarm_serialize_current_bake(const d8_swarm_t* swarm,
                                             d8_u8** out_blob, size_t* out_size);

/**
 * @brief Free serialized blob
 * @param blob Blob to free
 */
void d8_swarm_free_serialized_bake(d8_u8* blob);

/* ============================================================================
 * Shared buffer (for lock-free telemetry)
 * ============================================================================ */

/**
 * @brief Set shared buffer for telemetry
 * @param swarm Pointer to swarm
 * @param buffer Pointer to shared buffer (must be thread-safe)
 */
void d8_swarm_set_shared_buffer(d8_swarm_t* swarm, d8_shared_buffer_t* buffer);

/**
 * @brief Get shared buffer pointer
 * @param swarm Pointer to swarm
 * @return Pointer to shared buffer, or NULL
 */
d8_shared_buffer_t* d8_swarm_get_shared_buffer(d8_swarm_t* swarm);

/* ============================================================================
 * Double-strait bake (for cascade)
 * ============================================================================ */

/**
 * @brief Check if bake has double-strait flag
 * @param swarm Pointer to swarm
 * @return true if double-strait
 */
bool d8_swarm_is_double_strait_bake(const d8_swarm_t* swarm);

/**
 * @brief Set double-strait flag externally
 * @param swarm Pointer to swarm
 * @param enable true to enable
 */
void d8_swarm_set_double_strait_bake(d8_swarm_t* swarm, bool enable);

/* ============================================================================
 * Internal API (for swarm_core implementation)
 * ============================================================================ */

void d8_swarm_set_tile_params_direct(d8_swarm_t* swarm, size_t tile_id,
                                      d8_i16 thr_lo, d8_i16 thr_hi,
                                      d8_u16 decay16, d8_u8 domain_id, d8_u8 priority);

d8_i16 d8_swarm_get_tile_thr_lo_direct(const d8_swarm_t* swarm, size_t tile_id);
d8_i16 d8_swarm_get_tile_thr_hi_direct(const d8_swarm_t* swarm, size_t tile_id);
d8_u16 d8_swarm_get_tile_decay16_direct(const d8_swarm_t* swarm, size_t tile_id);
d8_u8 d8_swarm_get_tile_domain_id_direct(const d8_swarm_t* swarm, size_t tile_id);
d8_u8 d8_swarm_get_tile_priority_direct(const d8_swarm_t* swarm, size_t tile_id);
bool d8_swarm_get_tile_weight_sign_direct(const d8_swarm_t* swarm, size_t tile_id, size_t weight_idx);
d8_u8 d8_swarm_get_tile_weight_mag_direct(const d8_swarm_t* swarm, size_t tile_id, size_t weight_idx);
d8_u16 d8_swarm_get_tile_pattern_id_direct(const d8_swarm_t* swarm, size_t tile_id);
d8_u16 d8_swarm_get_tile_reset_on_fire_mask_direct(const d8_swarm_t* swarm, size_t tile_id);

/**
 * @brief Get tile weight visual representation (packed for clipboard)
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @param out_sign_mask64 Output sign mask (64 bits)
 * @param out_mag32 Output magnitudes (32 bytes, 2 nibbles per byte)
 */
void d8_swarm_get_tile_weight_visual(const d8_swarm_t* swarm, size_t tile_id,
                                      d8_u64* out_sign_mask64, d8_u8 out_mag32[32]);

void d8_swarm_set_tile_pattern_id_direct(d8_swarm_t* swarm, size_t tile_id, d8_u16 pattern_id);
void d8_swarm_set_tile_field_limit_direct(d8_swarm_t* swarm, d8_u32 limit);

/**
 * @brief Get underlying swarm pointer (for advanced operations)
 * @param swarm Pointer to swarm
 * @return Pointer to swarm
 */
d8_swarm_t* d8_swarm_get_swarm(d8_swarm_t* swarm);

/**
 * @brief Get tile parameters
 * @param swarm Pointer to swarm
 * @param tile_id Tile ID
 * @return Tile parameters structure
 */
d8_tile_params_t d8_swarm_get_tile_params(const d8_swarm_t* swarm, size_t tile_id);

/**
 * @brief Serialize current bake to blob
 * @param swarm Pointer to swarm
 * @param out_blob Output blob (caller must free via d8_swarm_free_serialized_bake)
 * @param out_size Output size
 * @return Status code
 */
d8_status_t d8_swarm_serialize_current_bake(const d8_swarm_t* swarm,
                                             d8_u8** out_blob, size_t* out_size);

#ifdef __cplusplus
}
#endif
