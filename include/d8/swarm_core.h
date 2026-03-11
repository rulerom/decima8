/*
 * DECIMA-8 Core - Pure C Implementation
 * Swarm Core API (unified interface for IDE)
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"
#include "d8/swarm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Swarm Core - unified interface
 * ============================================================================ */

/**
 * @brief Create swarm core instance
 * @return Pointer to core, or NULL on failure
 */
d8_swarm_core_t* d8_swarm_core_create(void);

/**
 * @brief Destroy swarm core instance
 * @param core Pointer to core
 */
void d8_swarm_core_destroy(d8_swarm_core_t* core);

/* ============================================================================
 * Bake operations
 * ============================================================================ */

/**
 * @brief Apply bake blob
 * @param core Pointer to core
 * @param blob Bake blob data
 * @param blob_size Size of blob
 * @return Status code
 */
d8_status_t d8_swarm_core_ev_bake(d8_swarm_core_t* core, const d8_u8* blob, size_t blob_size);

/**
 * @brief Check if bake is applied
 * @param core Pointer to core
 * @return true if bake is applied
 */
bool d8_swarm_core_is_baked(const d8_swarm_core_t* core);

/**
 * @brief Serialize current bake to blob
 * @param core Pointer to core
 * @param out_blob Output buffer (caller must free)
 * @param out_size Output size
 * @return Status code
 */
d8_status_t d8_swarm_core_serialize_bake(const d8_swarm_core_t* core,
                                          d8_u8** out_blob, size_t* out_size);

/**
 * @brief Free serialized bake blob
 * @param blob Blob to free
 */
void d8_swarm_core_free_bake_blob(d8_u8* blob);

/* ============================================================================
 * Flash operations
 * ============================================================================ */

/**
 * @brief Execute flash with ingress
 * @param core Pointer to core
 * @param tag Frame tag
 * @param ingress Input vector (8 lanes)
 * @return Flash result
 */
d8_flash_result_t d8_swarm_core_flash(d8_swarm_core_t* core, d8_u32 tag,
                                       const d8_u8 ingress[D8_K_LANES]);

/**
 * @brief Set VSB ingress (without flash)
 * @param core Pointer to core
 * @param ingress Input vector
 */
void d8_swarm_core_set_vsb_ingress(d8_swarm_core_t* core, const d8_u8 ingress[D8_K_LANES]);

/**
 * @brief Get last flash result
 * @param core Pointer to core
 * @return Flash result
 */
d8_flash_result_t d8_swarm_core_get_last_flash_result(const d8_swarm_core_t* core);

/* ============================================================================
 * Reset operations
 * ============================================================================ */

/**
 * @brief Reset domains by mask
 * @param core Pointer to core
 * @param mask16 Domain mask
 * @return Status code
 */
d8_status_t d8_swarm_core_ev_reset_domain(d8_swarm_core_t* core, d8_u16 mask16);

/* ============================================================================
 * Telemetry / Snapshot
 * ============================================================================ */

/**
 * @brief Get snapshot pointer (read-only)
 * @param core Pointer to core
 * @return Pointer to snapshot
 */
const d8_view_snapshot_t* d8_swarm_core_get_snapshot(const d8_swarm_core_t* core);

/**
 * @brief Get active tile count
 * @param core Pointer to core
 * @return Number of active tiles
 */
size_t d8_swarm_core_active_tile_count(const d8_swarm_core_t* core);

/**
 * @brief Set shared buffer for telemetry
 * @param core Pointer to core
 * @param buffer Pointer to shared buffer
 */
void d8_swarm_core_set_shared_buffer(d8_swarm_core_t* core, d8_shared_buffer_t* buffer);

/* ============================================================================
 * Tile queries (for IDE)
 * ============================================================================ */

/**
 * @brief Get tile locked state
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return true if locked
 */
bool d8_swarm_core_get_tile_locked(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile thr_cur
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return thr_cur value
 */
d8_i16 d8_swarm_core_get_tile_thr_cur(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile pattern_id
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return pattern_id
 */
d8_u16 d8_swarm_core_get_tile_pattern_id(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile routing masks
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return Routing masks
 */
d8_tile_routing_masks_t d8_swarm_core_get_tile_routing_masks(const d8_swarm_core_t* core, size_t tile_id);

/* ============================================================================
 * Tile modification (for IDE)
 * ============================================================================ */

/**
 * @brief Set tile locked state
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param locked true to lock
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_locked(d8_swarm_core_t* core, size_t tile_id, bool locked);

/**
 * @brief Set tile weight sign
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param weight_idx Weight index (0..63)
 * @param sign Sign (true=positive)
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_weight_sign(d8_swarm_core_t* core, size_t tile_id,
                                                size_t weight_idx, bool sign);

/**
 * @brief Set tile weight magnitude
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param weight_idx Weight index (0..63)
 * @param mag Magnitude (0..15)
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_weight_mag(d8_swarm_core_t* core, size_t tile_id,
                                               size_t weight_idx, d8_u8 mag);

/**
 * @brief Get tile parameters
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return Tile parameters structure
 */
d8_tile_params_t d8_swarm_core_get_tile_params(d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Set tile routing masks
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param masks Pointer to routing masks structure
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_routing_masks(d8_swarm_core_t* core, size_t tile_id,
                                                  const d8_tile_routing_masks_t* masks);

/**
 * @brief Set tile reset_on_fire_mask16
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param mask16 Reset mask
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_reset_on_fire_mask(d8_swarm_core_t* core, size_t tile_id,
                                                       d8_u16 mask16);

/**
 * @brief Set tile thr_lo/thr_hi/decay16/domain_id/priority
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param thr_lo Threshold low
 * @param thr_hi Threshold high
 * @param decay16 Decay value
 * @param domain_id Domain ID
 * @param priority Priority
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_params(d8_swarm_core_t* core, size_t tile_id,
                                           d8_i16 thr_lo, d8_i16 thr_hi,
                                           d8_u16 decay16, d8_u8 domain_id, d8_u8 priority);

/**
 * @brief Set tile pattern_id
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param pattern_id Pattern ID
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_pattern_id(d8_swarm_core_t* core, size_t tile_id, d8_u16 pattern_id);

/**
 * @brief Get tile thr_lo
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return thr_lo value
 */
d8_i16 d8_swarm_core_get_tile_thr_lo(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile thr_hi
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return thr_hi value
 */
d8_i16 d8_swarm_core_get_tile_thr_hi(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile decay16
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return decay16 value
 */
d8_u16 d8_swarm_core_get_tile_decay16(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile domain_id
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return domain_id value
 */
d8_u8 d8_swarm_core_get_tile_domain_id(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile priority
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @return priority value
 */
d8_u8 d8_swarm_core_get_tile_priority(const d8_swarm_core_t* core, size_t tile_id);

/**
 * @brief Get tile weight sign
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param weight_idx Weight index (0..63)
 * @return Sign (true=positive)
 */
bool d8_swarm_core_get_tile_weight_sign(const d8_swarm_core_t* core, size_t tile_id, size_t weight_idx);

/**
 * @brief Get tile weight magnitude
 * @param core Pointer to core
 * @param tile_id Tile ID
 * @param weight_idx Weight index (0..63)
 * @return Magnitude (0..15)
 */
d8_u8 d8_swarm_core_get_tile_weight_mag(const d8_swarm_core_t* core, size_t tile_id, size_t weight_idx);

/**
 * @brief Set tile field limit
 * @param core Pointer to core
 * @param limit Field limit value
 * @return Status code
 */
d8_status_t d8_swarm_core_set_tile_field_limit(d8_swarm_core_t* core, d8_u32 limit);

/**
 * @brief Get underlying swarm pointer (for advanced operations)
 * @param core Pointer to core
 * @return Pointer to swarm
 */
d8_swarm_t* d8_swarm_core_get_swarm(d8_swarm_core_t* core);

/**
 * @brief Serialize current bake to blob
 * @param core Pointer to core
 * @param out_blob Output blob (caller must free via d8_swarm_core_free_bake_blob)
 * @param out_size Output size
 * @return Status code
 */
d8_status_t d8_swarm_core_serialize_current_bake(d8_swarm_core_t* core,
                                                  d8_u8** out_blob, size_t* out_size);

/* ============================================================================
 * Double-strait bake (for cascade)
 * ============================================================================ */

/**
 * @brief Check if double-strait bake is active
 * @param core Pointer to core
 * @return true if double-strait
 */
bool d8_swarm_core_double_strait_bake(const d8_swarm_core_t* core);

/**
 * @brief Set double-strait flag
 * @param core Pointer to core
 * @param enable true to enable
 */
void d8_swarm_core_set_double_strait_bake(d8_swarm_core_t* core, bool enable);

#ifdef __cplusplus
}
#endif
