/*
 * DECIMA-8 Core - Pure C Implementation
 * Shared buffer for lock-free telemetry
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Tile state (8 bytes, aligned for cache)
 * ============================================================================ */

typedef struct {
    d8_u8 thr_cur16;      /* 0..15 (clamped positive) */
    d8_u8 locked_flag;    /* 0/1 */
    d8_u8 fire_event;     /* 0/1 (LOCK_TRANSITION in last flash) */
    d8_u8 _pad[5];        /* Padding for 8-byte alignment */
} d8_tile_state_t;

/* ============================================================================
 * Shared telemetry buffer
 * ============================================================================ */

typedef struct {
    /* Tile states: one per tile (4096 tiles) */
    d8_tile_state_t tiles[D8_K_TILE_COUNT];

    /* Frame metadata */
    volatile d8_u32 frame_tag;
    volatile d8_u32 flags32_last;
    volatile d8_u32 bake_id_active;
    volatile d8_u32 profile_id_active;
    volatile d8_u32 cycle_time_us;

    /* BUS readout */
    d8_u8 bus16[D8_K_LANES];
    d8_u8 bus_clip_mask8;

    /* Domain state */
    d8_u16 winner_tile_id[D8_K_DOMAINS];  /* 0xFFFF if none */
    d8_u16 auto_reset_mask16;
    d8_u16 collide_mask16;
    d8_u16 domains_fired_mask16;
} d8_shared_telemetry_buffer_t;

/* ============================================================================
 * Helper functions
 * ============================================================================ */

/**
 * @brief Write tile state atomically (core writes)
 * @param ts Pointer to tile state
 * @param thr_cur Threshold current (0..15)
 * @param locked Locked flag (0/1)
 * @param fire Fire event (0/1)
 */
static inline void d8_write_tile_state(d8_tile_state_t* ts,
                                        d8_u8 thr_cur, d8_u8 locked, d8_u8 fire) {
    ts->thr_cur16 = thr_cur;
    ts->locked_flag = locked;
    ts->fire_event = fire;
}

/**
 * @brief Read tile state (telemetry reads)
 * @param ts Pointer to tile state
 * @param out_thr Output: threshold current
 * @param out_locked Output: locked flag
 * @param out_fire Output: fire event
 */
static inline void d8_read_tile_state(const d8_tile_state_t* ts,
                                       d8_u8* out_thr, d8_u8* out_locked, d8_u8* out_fire) {
    *out_thr = ts->thr_cur16;
    *out_locked = ts->locked_flag;
    *out_fire = ts->fire_event;
}

/**
 * @brief Initialize shared buffer to default values
 * @param buf Pointer to buffer
 */
static inline void d8_shared_buffer_init(d8_shared_telemetry_buffer_t* buf) {
    /* Clear all tiles */
    for (size_t i = 0; i < D8_K_TILE_COUNT; i++) {
        d8_write_tile_state(&buf->tiles[i], 0, 0, 0);
    }

    /* Clear metadata */
    buf->frame_tag = 0;
    buf->flags32_last = 0;
    buf->bake_id_active = 0;
    buf->profile_id_active = 0;
    buf->cycle_time_us = 0;

    /* Clear BUS */
    for (size_t i = 0; i < D8_K_LANES; i++) {
        buf->bus16[i] = 0;
    }
    buf->bus_clip_mask8 = 0;

    /* Clear domain state */
    for (size_t i = 0; i < D8_K_DOMAINS; i++) {
        buf->winner_tile_id[i] = 0xFFFF;
    }
    buf->auto_reset_mask16 = 0;
    buf->collide_mask16 = 0;
    buf->domains_fired_mask16 = 0;
}

#ifdef __cplusplus
}
#endif
