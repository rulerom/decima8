/*
 * DECIMA-8 Core - Pure C Implementation
 * View Snapshot helpers for IDE telemetry
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * V-State helpers
 *
 * V-State packing (IDE):
 * [7:4] thr_norm (0..15) normalized thr_cur (v0.2 range-based)
 * [3]   Locked
 * [2]   Fire_Event (LOCK_TRANSITION in last flash)
 * [1:0] Polarity_Trend: 00=stable, 01=down, 10=up, 11=reserved
 * ============================================================================ */

/**
 * @brief Pack tile state into v_state byte
 * @param thr_norm Threshold normalized (0..15)
 * @param locked Locked flag (0/1)
 * @param fire Fire event (0/1)
 * @param trend Polarity trend (0..3)
 * @return Packed v_state byte
 */
static inline d8_u8 d8_pack_v_state(d8_u8 thr_norm, d8_u8 locked,
                                     d8_u8 fire, d8_u8 trend) {
    return ((thr_norm & 0x0F) << 4) |
           ((locked & 0x01) << 3) |
           ((fire & 0x01) << 2) |
           (trend & 0x03);
}

/**
 * @brief Unpack thr_norm from v_state
 * @param v_state Packed state byte
 * @return thr_norm (0..15)
 */
static inline d8_u8 d8_unpack_thr_norm(d8_u8 v_state) {
    return (v_state >> 4) & 0x0F;
}

/**
 * @brief Unpack locked flag from v_state
 * @param v_state Packed state byte
 * @return locked (0/1)
 */
static inline d8_u8 d8_unpack_locked(d8_u8 v_state) {
    return (v_state >> 3) & 0x01;
}

/**
 * @brief Unpack fire event from v_state
 * @param v_state Packed state byte
 * @return fire (0/1)
 */
static inline d8_u8 d8_unpack_fire(d8_u8 v_state) {
    return (v_state >> 2) & 0x01;
}

/**
 * @brief Unpack polarity trend from v_state
 * @param v_state Packed state byte
 * @return trend (0..3)
 */
static inline d8_u8 d8_unpack_trend(d8_u8 v_state) {
    return v_state & 0x03;
}

/**
 * @brief Normalize threshold to 4-bit (0..15) from 8-bit (0..255)
 * @param thr_8bit Threshold in 0..255 range
 * @return Normalized threshold in 0..15 range
 */
static inline d8_u8 d8_normalize_thr(d8_u8 thr_8bit) {
    return (d8_u8)((thr_8bit * 15) / 255);
}

#ifdef __cplusplus
}
#endif
