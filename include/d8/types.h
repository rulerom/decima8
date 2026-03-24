/*
 * DECIMA-8 Core - Pure C Implementation
 * This code is part of Decima-8 Core
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdalign.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Basic Types
 * ============================================================================ */

typedef uint8_t   d8_u8;
typedef uint16_t  d8_u16;
typedef uint32_t  d8_u32;
typedef uint64_t  d8_u64;
typedef int8_t    d8_i8;
typedef int16_t   d8_i16;
typedef int32_t   d8_i32;
typedef int64_t   d8_i64;

/* ============================================================================
 * Constants
 * ============================================================================ */

#define D8_K_LANES          8U
#define D8_K_DOMAINS        16U
#define D8_K_EXPECTED_W     128U
#define D8_K_EXPECTED_H     32U
#define D8_K_TILE_COUNT     (D8_K_EXPECTED_W * D8_K_EXPECTED_H)  /* 4096 */

#define D8_BAKE_FLAG_DOUBLE_STRAIT  (1U << 0)

/* TLV types (v0.2) */
#define D8_TLV_TOPOLOGY             0x0100U
#define D8_TLV_TILE_PARAMS_V2       0x0121U
#define D8_TLV_TILE_ROUTING_FLAGS16 0x0131U
#define D8_TLV_READOUT_POLICY       0x0140U
#define D8_TLV_RESET_ON_FIRE_MASK16 0x0150U
#define D8_TLV_TILE_WEIGHTS_PACKED  0x0160U
#define D8_TLV_TILE_FIELD_LIMIT     0x0170U
#define D8_TLV_CRC32                0xFFFEU

/* Magic and version */
#define D8_BAKE_MAGIC    {'D','8','B','K'}
#define D8_BAKE_VER_MAJOR 2U
#define D8_BAKE_VER_MINOR 0U

/* ============================================================================
 * Status codes
 * ============================================================================ */

typedef enum d8_status_code {
    D8_STATUS_OK = 0,
    D8_STATUS_NOT_BAKED = 1,
    D8_STATUS_BAD_PHASE = 2,
    D8_STATUS_BAD_INGRESS_LEVEL = 3,
    D8_STATUS_BAKE_BAD_MAGIC = 4,
    D8_STATUS_BAKE_BAD_VERSION = 5,
    D8_STATUS_BAKE_BAD_LEN = 6,
    D8_STATUS_BAKE_MISSING_TLV = 7,
    D8_STATUS_BAKE_BAD_TLV_LEN = 8,
    D8_STATUS_BAKE_CRC_FAIL = 9,
    D8_STATUS_BAKE_RESERVED_NON_ZERO = 10,
    D8_STATUS_TOPOLOGY_MISMATCH = 11,
    D8_STATUS_BAKE_NO_BLOB = 12,
    D8_STATUS_BAKE_BAD_LEN_TOTAL = 13
} d8_status_code_t;

typedef struct d8_status {
    d8_status_code_t code;
    d8_u32           extra;
    const char*      msg;
} d8_status_t;

#define D8_STATUS_OK_INIT {D8_STATUS_OK, 0, "OK"}

/* Helper macros for creating d8_status_t (C89/C90 compatible) */
#define D8_STATUS_MAKE(code, extra_val, msg_str) make_status(code, extra_val, msg_str)

static inline d8_status_t make_status(d8_status_code_t code, d8_u32 extra, const char* msg) {
    d8_status_t st;
    st.code = code;
    st.extra = extra;
    st.msg = msg;
    return st;
}

static inline bool d8_status_is_ok(const d8_status_t* st) {
    return st->code == D8_STATUS_OK;
}

/* ============================================================================
 * Readout mode
 * ============================================================================ */

typedef enum d8_readout_mode {
    D8_READOUT_R0_RAW_BUS = 0,
    D8_READOUT_R1_DOMAIN_WINNER_ID32 = 1
} d8_readout_mode_t;

/* ============================================================================
 * Bake structures
 * ============================================================================ */

typedef struct d8_bake_header {
    d8_u8  magic[4];
    d8_u16 ver_major;
    d8_u16 ver_minor;
    d8_u32 flags;
    d8_u32 total_len;
    d8_u32 bake_id;
    d8_u32 profile_id;
    d8_u32 reserved0;
} d8_bake_header_t;

typedef struct d8_topology {
    d8_u32 tile_count;
    d8_u16 tile_w;
    d8_u16 tile_h;
    d8_u8  lanes;
    d8_u8  domains;
    d8_u16 reserved;
    d8_u32 reserved2;
} d8_topology_t;

typedef struct d8_readout_policy {
    d8_u8  mode;
    d8_u8  reserved0;
    d8_u16 winner_domain_mask;
    d8_u16 settle_ns;
    d8_u16 reserved1;
    d8_u32 reserved2;
} d8_readout_policy_t;

/* ============================================================================
 * Snapshot / View structures
 * ============================================================================ */

typedef struct d8_view_snapshot {
    d8_u32 bake_id_active;
    d8_u32 profile_id_active;
    d8_u32 frame_tag;

    d8_u8  vsb_ingress16[D8_K_LANES];
    d8_u8  bus16[D8_K_LANES];

    d8_u8  v_state[D8_K_TILE_COUNT];

    d8_u16 winner_tile_id[D8_K_DOMAINS];
    d8_u16 winner_pattern_id[D8_K_DOMAINS];
    d8_u8  winner_priority[D8_K_DOMAINS];
    d8_u8  fired_cnt_sat[D8_K_DOMAINS];
    d8_u16 reset_mask_from_winner[D8_K_DOMAINS];

    d8_u16 auto_reset_mask16;
    d8_u16 collide_mask16;
    d8_u16 domains_fired_mask16;

    d8_u8  in_clip_bits[D8_K_TILE_COUNT];
    d8_u8  bus_clip_mask8;

    d8_u32 flags32_last;

    d8_u32 cycle_time_us;
    d8_u32 cycle_time_peak_us;
    d8_u32 cycle_time_avg_us;

    /* Domain connectivity for IDE */
    d8_u8  domain_conn4[D8_K_TILE_COUNT];

    /* VSB activity map */
    d8_u8  bus_prefix16[(D8_K_EXPECTED_W + 1) * D8_K_LANES];
    d8_u8  bus_prefix_clip_mask8[D8_K_EXPECTED_W + 1];
} d8_view_snapshot_t;

/* ============================================================================
 * Shared buffer for lock-free telemetry
 * ============================================================================ */

typedef struct d8_tile_state {
    d8_u8  thr_cur16;       /* 0..15 (clamped positive) */
    d8_u8  locked_flag;     /* 0/1 */
    d8_u8  fire_event;      /* 0/1 (LOCK_TRANSITION in last flash) */
    d8_u8  _pad[5];         /* Padding for 8-byte alignment */
} d8_tile_state_t;

typedef struct d8_shared_buffer {
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
    volatile d8_u16 auto_reset_mask16;
    volatile d8_u16 collide_mask16;
    volatile d8_u16 domains_fired_mask16;
} d8_shared_buffer_t;

/* ============================================================================
 * Flash result
 * ============================================================================ */

typedef struct d8_flash_result {
    d8_status_t           st;
    d8_u8                 readout16[D8_K_LANES];
    d8_u32                flags32_last;
} d8_flash_result_t;

/* ============================================================================
 * Tile parameters and routing masks
 * ============================================================================ */

typedef struct d8_tile_routing_masks {
    d8_u8 maskN;
    d8_u8 maskE;
    d8_u8 maskS;
    d8_u8 maskW;
    d8_u8 maskNE;
    d8_u8 maskSE;
    d8_u8 maskSW;
    d8_u8 maskNW;
    d8_u8 bus_w;
    d8_u8 bus_r;
} d8_tile_routing_masks_t;

typedef struct d8_tile_params {
    d8_i16 thr_lo;
    d8_i16 thr_hi;
    d8_u16 decay16;
    d8_u8  domain_id;
    d8_u8  priority;
    d8_u16 pattern_id;
    d8_u16 reset_on_fire_mask16;
} d8_tile_params_t;

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

typedef struct d8_swarm       d8_swarm_t;
typedef struct d8_swarm_core  d8_swarm_core_t;

/* ============================================================================
 * Utility functions
 * ============================================================================ */

static inline d8_u8 d8_clamp15_u8(int val) {
    return (d8_u8)((val < 0) ? 0 : ((val > 15) ? 15 : val));
}

static inline d8_i16 d8_clamp15_i16(int val) {
    return (d8_i16)((val < -32768) ? -32768 : ((val > 32767) ? 32767 : val));
}

#ifdef __cplusplus
}
#endif
