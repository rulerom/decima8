/*
 * DECIMA-8 Core - Pure C Implementation
 * Swarm implementation - FULL FLASH IMPLEMENTATION
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/swarm.h"
#include "d8/bake.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifdef _WIN32
#include <intrin.h>
#include <windows.h>

/* POSIX timers emulation for Windows */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif

struct timespec {
    long tv_sec;
    long tv_nsec;
};

static inline int clock_gettime(int clk_id, struct timespec* ts) {
    (void)clk_id;
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    ts->tv_sec = (long)(count.QuadPart / freq.QuadPart);
    ts->tv_nsec = (long)((count.QuadPart % freq.QuadPart) * 1000000000 / freq.QuadPart);
    return 0;
}
#else
#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>

/* Fallback for systems without clock_gettime */
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 199309L
/* clock_gettime is available */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC CLOCK_REALTIME
#endif
#else
/* Use gettimeofday as fallback */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif

static inline int clock_gettime(int clk_id, struct timespec* ts) {
    (void)clk_id;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
}
#endif
#endif

/* ============================================================================
 * Internal structures (expanded for flash)
 * ============================================================================ */

struct d8_swarm {
    /* Bake state */
    d8_i16  thr_lo[D8_K_TILE_COUNT];
    d8_i16  thr_hi[D8_K_TILE_COUNT];
    d8_u16  decay16[D8_K_TILE_COUNT];
    d8_u8   domain_id[D8_K_TILE_COUNT];
    d8_u8   priority[D8_K_TILE_COUNT];
    d8_u16  pattern_id[D8_K_TILE_COUNT];
    d8_u16  reset_on_fire_mask16[D8_K_TILE_COUNT];
    
    /* Routing masks */
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
    
    /* Weights (64 per tile) */
    d8_u8   w_mag[D8_K_TILE_COUNT * 64];
    d8_u8   w_sign[D8_K_TILE_COUNT * 64];
    
    /* Runtime state */
    d8_i16  thr_cur[D8_K_TILE_COUNT];
    d8_u8   locked[D8_K_TILE_COUNT];

    /* Cached activation flags (reset each flash cycle) */
    d8_u8   bus_r_allowed[D8_K_TILE_COUNT];  /* Can receive VSB from parent */
    d8_u8   bus_w_allowed[D8_K_TILE_COUNT];  /* Can write to BUS */

    /* Topology */
    d8_topology_t topo;
    d8_readout_policy_t readout;
    
    /* Active dimensions */
    size_t  active_tile_w;
    size_t  active_tile_h;
    size_t  active_tile_count;
    
    /* Bake metadata */
    d8_u32  bake_id;
    d8_u32  profile_id;
    d8_u32  bake_flags;
    bool    bake_applied;
    bool    double_strait_bake;
    
    /* Snapshot */
    d8_view_snapshot_t snapshot;
    
    /* Shared buffer */
    d8_shared_buffer_t* shared_buffer;
    
    /* Flash state */
    bool    flash_in_progress;
    
    /* Cycle time statistics */
    d8_u32  cycle_time_peak_us;
    d8_u64  cycle_time_sum_us;
    d8_u32  cycle_count;
    
    /* Internal flash buffers */
    d8_u8   drive_vec[D8_K_TILE_COUNT * 8];  /* 8 lanes per tile */
    d8_u8   in16[D8_K_TILE_COUNT * 8];
    d8_i16  row_raw[D8_K_TILE_COUNT * 8];
    d8_u8   row_out[D8_K_TILE_COUNT * 8];
    d8_i16  row_signed[D8_K_TILE_COUNT * 8];
    d8_u8   active[D8_K_TILE_COUNT];
    d8_u8   locked_before[D8_K_TILE_COUNT];
    d8_u8   fire[D8_K_TILE_COUNT];
};

/* ============================================================================
 * SIMD Helper Functions
 * ============================================================================ */

#if defined(D8_SIMD_SSE4_2) || defined(__SSE4_2__) || defined(_MSC_VER)
#define D8_HAS_SIMD 1
#include <smmintrin.h>  /* SSE4.2 */

/* Sum 8 i16 elements horizontally */
static inline d8_i16 simd_sum8_i16(const d8_i16* arr) {
    __m128i v = _mm_loadu_si128((const __m128i*)arr);
    v = _mm_hadd_epi16(v, v);
    v = _mm_hadd_epi16(v, v);
    v = _mm_hadd_epi16(v, v);
    return (d8_i16)_mm_cvtsi128_si32(v);
}

/* Conditional blend: if mask then a else b */
static inline void simd_blend8_u8(d8_u8* dst, const d8_u8* a, const d8_u8* b, d8_u8 mask) {
    __m128i va = _mm_loadu_si128((const __m128i*)a);
    __m128i vb = _mm_loadu_si128((const __m128i*)b);
    __m128i m = _mm_set1_epi8(mask ? -1 : 0);
    __m128i result = _mm_blendv_epi8(vb, va, m);
    _mm_storeu_si128((__m128i*)dst, result);
}

/* Clamp 8 u32 to 0..15 with clip mask */
static inline void simd_clamp15_8_u32(d8_u8* dst, d8_u8* clip_mask, const d8_u32* src) {
    __m128i v0 = _mm_loadu_si128((const __m128i*)src);
    __m128i v1 = _mm_loadu_si128((const __m128i*)(src + 4));
    __m128i fifteen = _mm_set1_epi32(15);
    
    /* Generate clip mask (src > 15) */
    __m128i clip0 = _mm_cmpgt_epi32(v0, fifteen);
    __m128i clip1 = _mm_cmpgt_epi32(v1, fifteen);
    
    /* Clamp: if v > 15, use 15, otherwise v */
    __m128i clamped0 = _mm_blendv_epi8(v0, fifteen, clip0);
    __m128i clamped1 = _mm_blendv_epi8(v1, fifteen, clip1);
    
    /* Pack to u8 */
    __m128i packed = _mm_packus_epi32(clamped0, clamped1);
    packed = _mm_packus_epi16(packed, packed);
    _mm_storeu_si128((__m128i*)dst, packed);
    
    /* Assemble clip mask */
    __m128i clip_packed = _mm_packs_epi32(clip0, clip1);
    clip_packed = _mm_packs_epi16(clip_packed, clip_packed);
    int clip_bits = _mm_movemask_epi8(clip_packed);
    *clip_mask = (d8_u8)(clip_bits & 0xFF);
}

#else
#define D8_HAS_SIMD 0
#endif

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

static bool is_active_tile(const d8_swarm_t* swarm, size_t tile_id);
static d8_u8 thr_norm_4bit(d8_i16 thr_cur, d8_i16 thr_lo, d8_i16 thr_hi);
static d8_u8 clamp15_u8(int val);
static void compute_in16(d8_swarm_t* swarm, size_t tile_id, const d8_u8* vsb_ingress, bool is_active);
static void compute_row_raw(d8_swarm_t* swarm, size_t tile_id);
static d8_i16 compute_delta_raw(d8_swarm_t* swarm, size_t tile_id);
static void compute_domain_connectivity(d8_swarm_t* swarm);
static void compute_parent_topology(d8_swarm_t* swarm);
static void write_to_shared_buffer(d8_swarm_t* swarm);
static d8_flash_result_t flash_impl(d8_swarm_t* swarm, d8_u32 tag, const d8_u8* vsb_ingress16);

/* ============================================================================
 * Creation / Destruction
 * ============================================================================ */

d8_swarm_t* d8_swarm_create(void) {
    d8_swarm_t* swarm = (d8_swarm_t*)calloc(1, sizeof(d8_swarm_t));
    if (!swarm) return NULL;
    
    /* Initialize with defaults */
    swarm->active_tile_w = D8_K_EXPECTED_W;
    swarm->active_tile_h = D8_K_EXPECTED_H;
    swarm->active_tile_count = D8_K_TILE_COUNT;
    
    swarm->topo.tile_count = D8_K_TILE_COUNT;
    swarm->topo.tile_w = D8_K_EXPECTED_W;
    swarm->topo.tile_h = D8_K_EXPECTED_H;
    swarm->topo.lanes = D8_K_LANES;
    swarm->topo.domains = D8_K_DOMAINS;
    
    return swarm;
}

void d8_swarm_destroy(d8_swarm_t* swarm) {
    if (swarm) {
        free(swarm);
    }
}

void d8_swarm_full_reset(d8_swarm_t* swarm) {
    if (!swarm) return;
    
    /* Reset runtime state */
    memset(swarm->thr_cur, 0, sizeof(swarm->thr_cur));
    memset(swarm->locked, 0, sizeof(swarm->locked));
    memset(&swarm->snapshot, 0, sizeof(swarm->snapshot));
    
    swarm->flash_in_progress = false;
    swarm->cycle_time_peak_us = 0;
    swarm->cycle_time_sum_us = 0;
    swarm->cycle_count = 0;
}

/* ============================================================================
 * Helper functions
 * ============================================================================ */

static bool is_active_tile(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return false;
    
    size_t x = tile_id % swarm->active_tile_w;
    size_t y = tile_id / swarm->active_tile_w;
    
    return (x < swarm->active_tile_w && y < swarm->active_tile_h);
}

static d8_u8 clamp15_u8(int val) {
    return (d8_u8)((val < 0) ? 0 : ((val > 15) ? 15 : val));
}

static d8_u8 thr_norm_4bit(d8_i16 thr_cur, d8_i16 thr_lo, d8_i16 thr_hi) {
    d8_i32 abs_lo = (thr_lo < 0) ? -thr_lo : thr_lo;
    d8_i32 abs_hi = (thr_hi < 0) ? -thr_hi : thr_hi;
    d8_i32 scale = (abs_lo > abs_hi) ? abs_lo : abs_hi;
    
    if (scale <= 0) return 15;
    
    if (thr_cur < 0) {
        d8_i32 neg_value = -thr_cur;
        d8_i32 clamped_neg = (neg_value < scale) ? neg_value : scale;
        d8_i32 norm = 8 + (clamped_neg * 7 + scale / 2) / scale;
        return (d8_u8)((norm < 8) ? 8 : ((norm > 15) ? 15 : norm));
    }
    
    d8_i32 pos_value = thr_cur;
    d8_i32 clamped_pos = (pos_value < scale) ? pos_value : scale;
    d8_i32 norm = (clamped_pos * 7 + scale / 2) / scale;
    return (d8_u8)((norm < 0) ? 0 : ((norm > 7) ? 7 : norm));
}

/* ============================================================================
 * Compute functions (PHASE_READ helpers)
 * ============================================================================ */

static void compute_in16(d8_swarm_t* swarm, size_t tile_id, const d8_u8* vsb_ingress, bool is_active) {
    d8_u8* in16_ptr = &swarm->in16[tile_id * 8];

    if (!is_active) {
        memset(in16_ptr, 0, 8);
        return;
    }
    
    /* VSB ingress only (v0.2: no BUS mixing) */
    for (int i = 0; i < 8; i++) {
        d8_u8 raw = vsb_ingress[i];
        in16_ptr[i] = clamp15_u8((int)raw);

        /* Set IN_CLIP if raw > 15 */
        if (raw > 15) {
            size_t byte_idx = tile_id / 8;
            size_t bit_idx = tile_id % 8;
            if (byte_idx < sizeof(swarm->snapshot.in_clip_bits)) {
                swarm->snapshot.in_clip_bits[byte_idx] |= (1u << bit_idx);
            }
        }
    }
}

static void compute_row_raw(d8_swarm_t* swarm, size_t tile_id) {
    d8_i16* row_raw_ptr = &swarm->row_raw[tile_id * 8];
    d8_u8* row_out_ptr = &swarm->row_out[tile_id * 8];
    d8_i16* row_signed_ptr = &swarm->row_signed[tile_id * 8];
    const d8_u8* in16_ptr = &swarm->in16[tile_id * 8];

    for (size_t r = 0; r < 8; r++) {
        size_t row_offset = tile_id * 64 + r * 8;
        d8_i16 sum = 0;

        for (size_t i = 0; i < 8; i++) {
            d8_u8 in_val = in16_ptr[i];
            d8_u8 mag_val = swarm->w_mag[row_offset + i];
            d8_u8 sign_val = swarm->w_sign[row_offset + i];

            /* Direct multiplication: in_val * mag_val */
            d8_i16 mul_result = sign_val ? (d8_i16)(in_val * mag_val) : (d8_i16)(-(d8_i16)(in_val * mag_val));
            sum += mul_result;
        }

        row_raw_ptr[r] = sum;
        row_signed_ptr[r] = sum;  /* For accumulator: no clamp */

        /* row_out for drive: (max(sum,0) + 7) / 8, clamp15 */
        int pos = (sum > 0) ? (int)sum : 0;
        row_out_ptr[r] = clamp15_u8((pos + 7) / 8);
    }
}

static d8_i16 compute_delta_raw(d8_swarm_t* swarm, size_t tile_id) {
    d8_i16* row_signed_ptr = &swarm->row_signed[tile_id * 8];
    d8_i16 delta = 0;

    for (size_t r = 0; r < 8; r++) {
        delta += row_signed_ptr[r];
    }
        
    return delta;
}

/* ============================================================================
 * Stub topology functions
 * ============================================================================ */

static void compute_domain_connectivity(d8_swarm_t* swarm) {
    (void)swarm;
    /* TODO: Implement domain connectivity */
}

static void compute_parent_topology(d8_swarm_t* swarm) {
    (void)swarm;
    /* TODO: Implement parent topology */
}

static void write_to_shared_buffer(d8_swarm_t* swarm) {
    if (!swarm || !swarm->shared_buffer) return;
    
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        d8_i16 thr_i16 = swarm->thr_cur[t];
        d8_u8 thr = clamp15_u8((thr_i16 > 0) ? thr_i16 : 0);
        d8_u8 locked = swarm->locked[t];
        d8_u8 fire = (swarm->snapshot.v_state[t] & 0x04) ? 1 : 0;
        
        swarm->shared_buffer->tile_state[t].thr_norm_4bit = thr;
        swarm->shared_buffer->tile_state[t].locked = locked;
        swarm->shared_buffer->tile_state[t].fire_event = fire;
    }
}

/* ============================================================================
 * FLASH IMPLEMENTATION (PHASE_READ → INTERPHASE → PHASE_WRITE)
 * ============================================================================ */

static d8_flash_result_t flash_impl(d8_swarm_t* swarm, d8_u32 tag, const d8_u8* vsb_ingress16) {
    d8_flash_result_t res = {0};

    if (!swarm || !vsb_ingress16) {
        res.st = D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL pointer");
        return res;
    }

    if (!swarm->bake_applied) {
        res.st = D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NotBaked");
        return res;
    }

    if (swarm->flash_in_progress) {
        res.st = D8_STATUS_MAKE(D8_STATUS_BAD_PHASE, 0, "Re-entrant EV_FLASH");
        return res;
    }
    
    /* Measure start time */
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    swarm->flash_in_progress = true;
    
    /* Initialize snapshot */
    swarm->snapshot.frame_tag = tag;
    memcpy(swarm->snapshot.vsb_ingress16, vsb_ingress16, 8);
    memset(&swarm->snapshot.bus16, 0, 8);
    swarm->snapshot.flags32_last = 0;
    swarm->snapshot.bus_clip_mask8 = 0;
    memset(swarm->snapshot.in_clip_bits, 0, sizeof(swarm->snapshot.in_clip_bits));
    
    /* ===================================================================
     * PHASE 1: PHASE_READ - OPTIMIZED MULTI-PASS ACTIVATION
     * =================================================================== */

    /* Reset cached activation flags */
    memset(swarm->bus_r_allowed, 0, swarm->active_tile_count);
    memset(swarm->bus_w_allowed, 0, swarm->active_tile_count);
    memset(swarm->active, 0, sizeof(swarm->active));

    /* Seed: tiles with BUS_R set from bake */
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        if (swarm->bus_r[t]) {
            swarm->bus_r_allowed[t] = 1;
            swarm->active[t] = 1;
        }
    }

    /* Multi-pass propagation: locked tiles activate their children directly */
    /* Repeat until no new activations (typically 2-4 passes for deep chains) */
    bool changed = true;
    while (changed) {
        changed = false;
        size_t tile_w = swarm->active_tile_w;
        
        for (size_t t = 0; t < swarm->active_tile_count; t++) {
            /* Skip if not active or not locked */
            if (!swarm->active[t] || !swarm->locked[t]) continue;

            /* Activate children directly based on routing masks */
            /* East child */
            if (swarm->maskE[t] && (t % tile_w) < tile_w - 1 && t + 1 < swarm->active_tile_count) {
                if (!swarm->active[t + 1]) {
                    swarm->bus_r_allowed[t + 1] = 1;
                    swarm->active[t + 1] = 1;
                    changed = true;
                }
            }
            /* West child */
            if (swarm->maskW[t] && (t % tile_w) > 0) {
                if (!swarm->active[t - 1]) {
                    swarm->bus_r_allowed[t - 1] = 1;
                    swarm->active[t - 1] = 1;
                    changed = true;
                }
            }
            /* South child */
            if (swarm->maskS[t] && t + tile_w < swarm->active_tile_count) {
                if (!swarm->active[t + tile_w]) {
                    swarm->bus_r_allowed[t + tile_w] = 1;
                    swarm->active[t + tile_w] = 1;
                    changed = true;
                }
            }
            /* North child */
            if (swarm->maskN[t] && t >= tile_w) {
                if (!swarm->active[t - tile_w]) {
                    swarm->bus_r_allowed[t - tile_w] = 1;
                    swarm->active[t - tile_w] = 1;
                    changed = true;
                }
            }
            /* SE child */
            if (swarm->maskSE[t] && (t % tile_w) < tile_w - 1 && t + tile_w + 1 < swarm->active_tile_count) {
                if (!swarm->active[t + tile_w + 1]) {
                    swarm->bus_r_allowed[t + tile_w + 1] = 1;
                    swarm->active[t + tile_w + 1] = 1;
                    changed = true;
                }
            }
            /* SW child */
            if (swarm->maskSW[t] && (t % tile_w) > 0 && t + tile_w - 1 < swarm->active_tile_count) {
                if (!swarm->active[t + tile_w - 1]) {
                    swarm->bus_r_allowed[t + tile_w - 1] = 1;
                    swarm->active[t + tile_w - 1] = 1;
                    changed = true;
                }
            }
            /* NE child */
            if (swarm->maskNE[t] && (t % tile_w) < tile_w - 1 && t - tile_w + 1 < swarm->active_tile_count) {
                if (!swarm->active[t - tile_w + 1]) {
                    swarm->bus_r_allowed[t - tile_w + 1] = 1;
                    swarm->active[t - tile_w + 1] = 1;
                    changed = true;
                }
            }
            /* NW child */
            if (swarm->maskNW[t] && (t % tile_w) > 0 && t >= tile_w) {
                if (!swarm->active[t - tile_w - 1]) {
                    swarm->bus_r_allowed[t - tile_w - 1] = 1;
                    swarm->active[t - tile_w - 1] = 1;
                    changed = true;
                }
            }
        }
    }

    /* Take snapshot of locked_before */
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        swarm->locked_before[t] = swarm->locked[t];
    }

    /* Process all active tiles */
    int first_active_logged = 0;
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        bool is_active = (swarm->active[t] != 0);
        bool is_locked_before = (swarm->locked_before[t] != 0);

        /* Locked tiles are always "active" for processing (they just don't accumulate) */
        if (is_locked_before) is_active = 1;

        if (is_active && !first_active_logged) {
            first_active_logged = 1;
        }

        /* Compute in16 */
        compute_in16(swarm, t, vsb_ingress16, is_active);

        /* If not active, force reset */
        if (!is_active) {
            swarm->thr_cur[t] = 0;
            swarm->locked[t] = 0;
            continue;
        }

        /* If locked_before, skip accumulation (but apply decay) */
        if (swarm->locked_before[t]) {
            /* Apply decay to locked tiles - NO delta accumulation */
            d8_u16 decay16 = swarm->decay16[t];
            d8_i16 thr = swarm->thr_cur[t];
            
            if (decay16 > 0) {
                if (thr > 0) {
                    thr = (d8_i16)(thr - decay16);
                    if (thr < 0) thr = 0;
                } else if (thr < 0) {
                    thr = (d8_i16)(thr + decay16);
                    if (thr > 0) thr = 0;
                }
            }
            
            swarm->thr_cur[t] = thr;

            /* Check if this tile should unlock (thr_cur fell below thr_lo) */
            d8_i16 thr_lo = swarm->thr_lo[t];
            d8_i16 thr_hi = swarm->thr_hi[t];
            bool range_active = (thr_lo < thr_hi);
            /* Unlock when |thr_cur| < |thr_lo| */
            bool in_range = range_active && (swarm->thr_cur[t] >= thr_lo || swarm->thr_cur[t] <= -thr_lo);

            if (!in_range) {
                swarm->locked[t] = 0;  /* Unlock tile */
                continue;
            }

            /* Check if this tile has bus_r_allowed (from parent or bake) */
            /* If not, unlock this tile (cascade unlock) */
            if (!swarm->bus_r_allowed[t]) {
                /* Cascade unlock - but NOT for seed tiles (bus_r=1 from bake) */
                if (!swarm->bus_r[t]) {
                    swarm->locked[t] = 0;
                }
            }

            /* locked_before tile remains locked - preserve locked state */
            swarm->locked[t] = 1;
            continue;  /* Skip delta accumulation for locked_before tiles */
        }
        
        /* Compute row pipeline - ONLY for non-locked_before tiles */
        compute_row_raw(swarm, t);

        /* Compute delta - ONLY for non-locked_before tiles */
        d8_i16 delta = compute_delta_raw(swarm, t);
        
        /* Update thr_cur with decay */
        d8_i16 thr_before = swarm->thr_cur[t];
        d8_i64 thr_tmp = (d8_i64)thr_before + (d8_i64)delta;
        
        /* Decay toward 0 */
        d8_u16 decay16 = swarm->decay16[t];
        if (decay16 > 0) {
            if (thr_tmp > 0) {
                thr_tmp = (thr_tmp - (d8_i64)decay16 > 0) ? thr_tmp - (d8_i64)decay16 : 0;
            } else if (thr_tmp < 0) {
                thr_tmp = (thr_tmp + (d8_i64)decay16 < 0) ? thr_tmp + (d8_i64)decay16 : 0;
            }
        }
        
        /* Clamp to i16 */
        swarm->thr_cur[t] = (d8_i16)((thr_tmp < -32768) ? -32768 : ((thr_tmp > 32767) ? 32767 : thr_tmp));

        /* FUSE-LOCK by range */
        d8_i16 thr_lo = swarm->thr_lo[t];
        d8_i16 thr_hi = swarm->thr_hi[t];
        
        /* Lock when thr_cur is within [thr_lo, thr_hi] range (or [-thr_hi, -thr_lo] for negative) */
        bool in_range = (swarm->thr_cur[t] >= thr_lo && swarm->thr_cur[t] <= thr_hi);
        
        bool has_signal = (delta != 0);

        if (swarm->bake_id > 0 && in_range && has_signal) {
            swarm->locked[t] = 1;
        }
    }
    
    /* ===================================================================
     * PHASE 2: INTERPHASE (winner selection, auto-reset)
     * =================================================================== */
    
    /* Simplified: no collision detection for now */
    swarm->snapshot.collide_mask16 = 0;
    swarm->snapshot.auto_reset_mask16 = 0;
    
    /* ===================================================================
     * PHASE 3: PHASE_WRITE
     * =================================================================== */
    
    /* Drive selection and BUS accumulation */
    d8_u32 bus_raw[8] = {0};
    
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        if (!swarm->bus_w[t]) continue;
        
        /* Check if tile can write (locked self or locked ancestor) */
        bool can_write = (swarm->locked[t] != 0);
        if (!can_write) continue;
        
        /* Drive selection: if locked, passthrough in16; else row_out */
        d8_u8* drive_vec = &swarm->drive_vec[t * 8];
        const d8_u8* in16_ptr = &swarm->in16[t * 8];
        const d8_u8* row_out_ptr = &swarm->row_out[t * 8];
        
        for (int i = 0; i < 8; i++) {
            drive_vec[i] = swarm->locked[t] ? in16_ptr[i] : row_out_ptr[i];
            bus_raw[i] += drive_vec[i];
        }
    }

    /* Clamp BUS to 0..15 */
    d8_u8 clip_mask = 0;
#if D8_HAS_SIMD
    simd_clamp15_8_u32(swarm->snapshot.bus16, &clip_mask, bus_raw);
#else
    for (int i = 0; i < 8; i++) {
        swarm->snapshot.bus16[i] = clamp15_u8((int)bus_raw[i]);
        if (bus_raw[i] > 15) clip_mask |= (1u << i);
    }
#endif
    
    if (clip_mask != 0) {
        swarm->snapshot.bus_clip_mask8 = clip_mask;
    }
    
    /* ===================================================================
     * READOUT_SAMPLE
     * =================================================================== */

    d8_u8 readout[8];
    if (swarm->readout.mode == D8_READOUT_R0_RAW_BUS) {
        memcpy(readout, swarm->snapshot.bus16, 8);
    } else {
        /* R1_DOMAIN_WINNER_ID32 - simplified */
        memset(readout, 0, 8);
    }

    /* ===================================================================
     * WINNER SELECTION (for each domain, find locked tile with highest thr_cur)
     * =================================================================== */

    /* Initialize winners to 0xFFFF (no winner) */
    for (int d = 0; d < 16; d++) {
        swarm->snapshot.winner_tile_id[d] = 0xFFFF;
        swarm->snapshot.winner_pattern_id[d] = 0;
        swarm->snapshot.fired_cnt_sat[d] = 0;
        swarm->snapshot.reset_mask_from_winner[d] = 0;
    }

    /* Find winner for each domain */
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        d8_u8 d = swarm->domain_id[t];
        if (d >= 16) continue;

        /* Skip tiles with pattern_id=0 - they are intermediate/relay tiles, not final winners */
        if (swarm->pattern_id[t] == 0) continue;

        /* Check if thr_cur is in range [thr_lo, thr_hi] */
        d8_i16 thr_lo = swarm->thr_lo[t];
        d8_i16 thr_hi = swarm->thr_hi[t];
        bool in_range = (swarm->thr_cur[t] >= thr_lo && swarm->thr_cur[t] <= thr_hi);

        if (!in_range) continue;  /* Tile not in fuse-lock range */

        /* Check if this tile has higher thr_cur than current winner */
        d8_u16 current_winner = swarm->snapshot.winner_tile_id[d];

        if (current_winner == 0xFFFF || swarm->thr_cur[t] > swarm->thr_cur[current_winner]) {
            swarm->snapshot.winner_tile_id[d] = (d8_u16)t;
            swarm->snapshot.winner_pattern_id[d] = swarm->pattern_id[t];
        }
    }

    /* ===================================================================
     * FLAGS32_LAST
     * =================================================================== */
    
    bool ovf_any = (clip_mask != 0);
    bool collide_any = (swarm->snapshot.collide_mask16 != 0);
    
    d8_u32 flags = 1u;  /* READY_LAST */
    if (ovf_any) flags |= (1u << 1);  /* OVF_ANY_LAST */
    if (collide_any) flags |= (1u << 2);  /* COLLIDE_ANY_LAST */
    flags |= ((d8_u32)swarm->snapshot.collide_mask16 << 16);
    
    swarm->snapshot.flags32_last = flags;
    
    /* ===================================================================
     * Update v_state for all tiles
     * =================================================================== */

    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        d8_u8 thr_n = thr_norm_4bit(swarm->thr_cur[t], swarm->thr_lo[t], swarm->thr_hi[t]);
        d8_u8 is_locked = (swarm->bake_id > 0) ? (swarm->locked[t] != 0) : 0;
        d8_u8 fire_bit = swarm->fire[t] ? 0x04 : 0;

        swarm->snapshot.v_state[t] = (d8_u8)((thr_n << 4) | (is_locked ? 0x08 : 0) | fire_bit);
    }
    
    /* ===================================================================
     * Cycle time statistics
     * =================================================================== */
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    d8_u32 cycle_time_us = (d8_u32)((end_time.tv_nsec - start_time.tv_nsec) / 1000 + (end_time.tv_sec - start_time.tv_sec) * 1000000);
    
    swarm->snapshot.cycle_time_us = cycle_time_us;
    if (cycle_time_us > swarm->cycle_time_peak_us) {
        swarm->cycle_time_peak_us = cycle_time_us;
    }
    swarm->snapshot.cycle_time_peak_us = swarm->cycle_time_peak_us;
    
    swarm->cycle_time_sum_us += cycle_time_us;
    swarm->cycle_count++;
    if (swarm->cycle_count > 1000) {
        swarm->cycle_time_sum_us = (swarm->cycle_time_sum_us * 999) / 1000 + cycle_time_us;
        swarm->cycle_count = 1000;
    }
    swarm->snapshot.cycle_time_avg_us = (d8_u32)(swarm->cycle_time_sum_us / swarm->cycle_count);
    
    /* Write to shared buffer */
    write_to_shared_buffer(swarm);

    swarm->flash_in_progress = false;

    /* Fill result */
    res.st = D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
    memcpy(res.readout16, readout, 8);
    res.flags32_last = flags;
    
    return res;
}

/* ============================================================================
 * Public API - Flash operations
 * ============================================================================ */

d8_flash_result_t d8_swarm_ev_flash(d8_swarm_t* swarm, d8_u32 tag, const d8_u8 vsb_ingress16[D8_K_LANES]) {
    if (!swarm || !vsb_ingress16) {
        d8_flash_result_t res = {0};
        res.st = D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL");
        return res;
    }
    
    /* Double-strait bake: two flashes, return second result */
    if (swarm->double_strait_bake) {
        /* First flash (result ignored) */
        flash_impl(swarm, tag, vsb_ingress16);
        
        /* Second flash (result returned) */
        return flash_impl(swarm, tag, vsb_ingress16);
    }
    
    return flash_impl(swarm, tag, vsb_ingress16);
}

d8_flash_result_t d8_swarm_set_vsb_and_flash(d8_swarm_t* swarm, d8_u32 tag, const d8_u8 vsb_ingress16[D8_K_LANES]) {
    return d8_swarm_ev_flash(swarm, tag, vsb_ingress16);
}

d8_status_t d8_swarm_ev_reset_domain(d8_swarm_t* swarm, d8_u16 mask16) {
    if (!swarm || !swarm->bake_applied) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "Not baked");
    }
    if (swarm->flash_in_progress) {
        return D8_STATUS_MAKE(D8_STATUS_BAD_PHASE, 0, "Flash in progress");
    }

    /* Reset tiles in domains specified by mask */
    for (size_t t = 0; t < swarm->active_tile_count; t++) {
        d8_u8 d = swarm->domain_id[t];
        if (mask16 & (1u << d)) {
            swarm->thr_cur[t] = 0;
            swarm->locked[t] = 0;
            
            /* Update snapshot v_state immediately */
            d8_u8 thr_n = 0;  /* thr_cur=0 -> thr_n=0 */
            swarm->snapshot.v_state[t] = (d8_u8)((thr_n << 4) | 0 | 0);  /* locked=0, fire=0 */
        }
    }

    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

/* ============================================================================
 * Getter/Setter functions (for swarm_core)
 * ============================================================================ */

d8_status_t d8_swarm_ev_bake(d8_swarm_t* swarm, const d8_u8* blob, size_t blob_size) {
    if (!swarm || !blob || blob_size == 0) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "NULL pointer");
    }

    /* Parse bake blob - allocate on heap to avoid stack overflow */
    d8_bake_view_t* view = (d8_bake_view_t*)malloc(sizeof(d8_bake_view_t));
    if (!view) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "Memory allocation failed");
    }
    
    d8_status_t st = d8_bake_validate(blob, blob_size, view);
    if (!d8_status_is_ok(&st)) {
        free(view);
        return st;
    }

    /* Apply topology */
    swarm->topo = view->topo;
    
    /* Restore active_tile_count from tile_field_limit or tile_count */
    if (view->tile_field_limit > 0 && view->tile_field_limit <= D8_K_TILE_COUNT) {
        swarm->active_tile_count = view->tile_field_limit;
    } else if (view->topo.reserved2 > 0 && view->topo.reserved2 <= D8_K_TILE_COUNT) {
        swarm->active_tile_count = view->topo.reserved2;
    } else {
        swarm->active_tile_count = view->tile_count;
    }
    
    /* Compute active_tile_w and active_tile_h from active_tile_count */
    /* Use the same logic as IDE's compute_active_dims */
    if (swarm->active_tile_count == 0 || swarm->active_tile_count == D8_K_TILE_COUNT) {
        swarm->active_tile_w = D8_K_EXPECTED_W;  /* 128 */
        swarm->active_tile_h = D8_K_EXPECTED_H;  /* 32 */
    } else {
        switch (swarm->active_tile_count) {
        case 256:  swarm->active_tile_w = 32;  swarm->active_tile_h = 8;  break;
        case 512:  swarm->active_tile_w = 32;  swarm->active_tile_h = 16; break;
        case 1024: swarm->active_tile_w = 64;  swarm->active_tile_h = 16; break;
        case 2048: swarm->active_tile_w = 64;  swarm->active_tile_h = 32; break;
        case 4096: swarm->active_tile_w = 128; swarm->active_tile_h = 32; break;
        default:
            /* Fallback: use topology from file */
            swarm->active_tile_w = view->topo.tile_w;
            swarm->active_tile_h = view->topo.tile_h;
            break;
        }
    }

    /* Apply tile parameters */
    memcpy(swarm->thr_lo, view->thr_lo, sizeof(d8_i16) * view->tile_count);
    memcpy(swarm->thr_hi, view->thr_hi, sizeof(d8_i16) * view->tile_count);
    memcpy(swarm->decay16, view->decay16, sizeof(d8_u16) * view->tile_count);
    memcpy(swarm->domain_id, view->domain_id, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->priority, view->priority, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->pattern_id, view->pattern_id, sizeof(d8_u16) * view->tile_count);
    memcpy(swarm->reset_on_fire_mask16, view->reset_on_fire_mask16, sizeof(d8_u16) * view->tile_count);

    /* Apply routing flags */
    memcpy(swarm->maskN, view->maskN, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskE, view->maskE, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskS, view->maskS, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskW, view->maskW, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskNE, view->maskNE, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskSE, view->maskSE, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskSW, view->maskSW, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->maskNW, view->maskNW, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->bus_w, view->bus_w, sizeof(d8_u8) * view->tile_count);
    memcpy(swarm->bus_r, view->bus_r, sizeof(d8_u8) * view->tile_count);

    /* Apply weights */
    memcpy(swarm->w_mag, view->w_mag, sizeof(d8_u8) * view->tile_count * 64);
    memcpy(swarm->w_sign, view->w_sign, sizeof(d8_u8) * view->tile_count * 64);

    /* Apply readout policy */
    swarm->readout = view->readout;

    /* Mark as baked */
    swarm->bake_applied = true;
    swarm->bake_id = view->bake_id;
    swarm->profile_id = view->profile_id;
    swarm->bake_flags = view->bake_flags;

    free(view);
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

size_t d8_swarm_get_active_tile_count(const d8_swarm_t* swarm) {
    if (!swarm) return 0;
    return swarm->active_tile_count;
}

void d8_swarm_set_shared_buffer(d8_swarm_t* swarm, d8_shared_buffer_t* buffer) {
    if (!swarm) return;
    swarm->shared_buffer = buffer;
}

const d8_view_snapshot_t* d8_swarm_get_snapshot(const d8_swarm_t* swarm) {
    if (!swarm) return NULL;
    return &swarm->snapshot;
}

void d8_swarm_set_snapshot_bake_id(d8_swarm_t* swarm, d8_u32 bake_id, d8_u32 profile_id) {
    if (!swarm) return;
    swarm->snapshot.bake_id_active = bake_id;
    swarm->snapshot.profile_id_active = profile_id;
}

void d8_swarm_update_shared_buffer_bake_id(d8_swarm_t* swarm, d8_u32 bake_id, d8_u32 profile_id) {
    if (!swarm || !swarm->shared_buffer) return;
    swarm->shared_buffer->bake_id_active = bake_id;
    swarm->shared_buffer->profile_id_active = profile_id;
}

void d8_swarm_set_bake_id(d8_swarm_t* swarm, d8_u32 bake_id, d8_u32 profile_id) {
    if (!swarm) return;
    swarm->bake_id = bake_id;
    swarm->profile_id = profile_id;
}

bool d8_swarm_get_tile_locked(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return false;
    return swarm->locked[tile_id] != 0;
}

d8_i16 d8_swarm_get_tile_thr_cur(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->thr_cur[tile_id];
}

d8_u16 d8_swarm_get_tile_pattern_id(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->pattern_id[tile_id];
}

d8_tile_routing_masks_t d8_swarm_get_tile_routing_masks(const d8_swarm_t* swarm, size_t tile_id) {
    d8_tile_routing_masks_t masks = {0};
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return masks;
    masks.maskN = swarm->maskN[tile_id];
    masks.maskE = swarm->maskE[tile_id];
    masks.maskS = swarm->maskS[tile_id];
    masks.maskW = swarm->maskW[tile_id];
    masks.maskNE = swarm->maskNE[tile_id];
    masks.maskSE = swarm->maskSE[tile_id];
    masks.maskSW = swarm->maskSW[tile_id];
    masks.maskNW = swarm->maskNW[tile_id];
    masks.bus_w = swarm->bus_w[tile_id];
    masks.bus_r = swarm->bus_r[tile_id];
    return masks;
}

d8_status_t d8_swarm_set_tile_locked(d8_swarm_t* swarm, size_t tile_id, bool locked) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    swarm->locked[tile_id] = locked ? 1 : 0;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_status_t d8_swarm_set_tile_weight_sign(d8_swarm_t* swarm, size_t tile_id,
                                           size_t weight_idx, bool sign) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad index");
    }
    swarm->w_sign[tile_id * 64 + weight_idx] = sign ? 1 : 0;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_status_t d8_swarm_set_tile_weight_mag(d8_swarm_t* swarm, size_t tile_id,
                                          size_t weight_idx, d8_u8 mag) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad index");
    }
    if (mag > 15) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_RESERVED_NON_ZERO, mag, "Magnitude > 15");
    }
    swarm->w_mag[tile_id * 64 + weight_idx] = mag;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_status_t d8_swarm_set_tile_routing_masks(d8_swarm_t* swarm, size_t tile_id,
                                             d8_u8 maskN, d8_u8 maskE, d8_u8 maskS, d8_u8 maskW,
                                             d8_u8 maskNE, d8_u8 maskSE, d8_u8 maskSW, d8_u8 maskNW,
                                             d8_u8 bus_w, d8_u8 bus_r) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    swarm->maskN[tile_id] = maskN;
    swarm->maskE[tile_id] = maskE;
    swarm->maskS[tile_id] = maskS;
    swarm->maskW[tile_id] = maskW;
    swarm->maskNE[tile_id] = maskNE;
    swarm->maskSE[tile_id] = maskSE;
    swarm->maskSW[tile_id] = maskSW;
    swarm->maskNW[tile_id] = maskNW;
    swarm->bus_w[tile_id] = bus_w;
    swarm->bus_r[tile_id] = bus_r;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_status_t d8_swarm_set_tile_reset_on_fire_mask(d8_swarm_t* swarm, size_t tile_id,
                                                  d8_u16 mask16) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    swarm->reset_on_fire_mask16[tile_id] = mask16;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

void d8_swarm_set_double_strait_bake(d8_swarm_t* swarm, bool enable) {
    if (!swarm) return;
    swarm->double_strait_bake = enable;
}

bool d8_swarm_is_double_strait_bake(const d8_swarm_t* swarm) {
    if (!swarm) return false;
    return swarm->double_strait_bake;
}

bool d8_swarm_is_baked(const d8_swarm_t* swarm) {
    if (!swarm) return false;
    return swarm->bake_applied;
}

d8_shared_buffer_t* d8_swarm_get_shared_buffer(d8_swarm_t* swarm) {
    if (!swarm) return NULL;
    return swarm->shared_buffer;
}

/* ============================================================================
 * Internal API (for swarm_core implementation)
 * ============================================================================ */

void d8_swarm_set_tile_params_direct(d8_swarm_t* swarm, size_t tile_id,
                                      d8_i16 thr_lo, d8_i16 thr_hi,
                                      d8_u16 decay16, d8_u8 domain_id, d8_u8 priority) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return;
    swarm->thr_lo[tile_id] = thr_lo;
    swarm->thr_hi[tile_id] = thr_hi;
    swarm->decay16[tile_id] = decay16;
    swarm->domain_id[tile_id] = domain_id;
    swarm->priority[tile_id] = priority;
}

d8_i16 d8_swarm_get_tile_thr_lo_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->thr_lo[tile_id];
}

d8_i16 d8_swarm_get_tile_thr_hi_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->thr_hi[tile_id];
}

d8_u16 d8_swarm_get_tile_decay16_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->decay16[tile_id];
}

d8_u8 d8_swarm_get_tile_domain_id_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->domain_id[tile_id];
}

d8_u8 d8_swarm_get_tile_priority_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->priority[tile_id];
}

bool d8_swarm_get_tile_weight_sign_direct(const d8_swarm_t* swarm, size_t tile_id, size_t weight_idx) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) return false;
    return swarm->w_sign[tile_id * 64 + weight_idx] != 0;
}

d8_u8 d8_swarm_get_tile_weight_mag_direct(const d8_swarm_t* swarm, size_t tile_id, size_t weight_idx) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) return 0;
    return swarm->w_mag[tile_id * 64 + weight_idx];
}

d8_u16 d8_swarm_get_tile_pattern_id_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->pattern_id[tile_id];
}

d8_u16 d8_swarm_get_tile_reset_on_fire_mask_direct(const d8_swarm_t* swarm, size_t tile_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return swarm->reset_on_fire_mask16[tile_id];
}

void d8_swarm_get_tile_weight_visual(const d8_swarm_t* swarm, size_t tile_id,
                                      d8_u64* out_sign_mask64, d8_u8 out_mag32[32]) {
    if (!out_sign_mask64 || !out_mag32) {
        return;
    }
    
    *out_sign_mask64 = 0;
    memset(out_mag32, 0, 32);
    
    if (!swarm || tile_id >= D8_K_TILE_COUNT) {
        return;
    }
    
    /* Pack 64 signs into 64-bit mask */
    for (size_t i = 0; i < 64; ++i) {
        if (swarm->w_sign[tile_id * 64 + i]) {
            *out_sign_mask64 |= (1ULL << i);
        }
    }
    
    /* Pack 64 magnitudes into 32 bytes (2 nibbles per byte) */
    for (size_t i = 0; i < 32; ++i) {
        d8_u8 lo = swarm->w_mag[tile_id * 64 + i * 2] & 0x0F;
        d8_u8 hi = swarm->w_mag[tile_id * 64 + i * 2 + 1] & 0x0F;
        out_mag32[i] = (hi << 4) | lo;
    }
}

void d8_swarm_set_tile_pattern_id_direct(d8_swarm_t* swarm, size_t tile_id, d8_u16 pattern_id) {
    if (!swarm || tile_id >= D8_K_TILE_COUNT) return;
    swarm->pattern_id[tile_id] = pattern_id;
}

void d8_swarm_set_tile_field_limit_direct(d8_swarm_t* swarm, d8_u32 limit) {
    if (!swarm) return;
    swarm->topo.reserved2 = limit;

    /* Update active_tile_count based on limit */
    if (limit > 0 && limit < D8_K_TILE_COUNT) {
        swarm->active_tile_count = limit;
    } else if (limit >= D8_K_TILE_COUNT) {
        swarm->active_tile_count = D8_K_TILE_COUNT;
    }
    
    /* Compute active_tile_w and active_tile_h from active_tile_count */
    /* Use the same logic as in d8_swarm_ev_bake and IDE's compute_active_dims */
    if (swarm->active_tile_count == 0 || swarm->active_tile_count == D8_K_TILE_COUNT) {
        swarm->active_tile_w = D8_K_EXPECTED_W;  /* 128 */
        swarm->active_tile_h = D8_K_EXPECTED_H;  /* 32 */
    } else {
        switch (swarm->active_tile_count) {
        case 256:  swarm->active_tile_w = 32;  swarm->active_tile_h = 8;  break;
        case 512:  swarm->active_tile_w = 32;  swarm->active_tile_h = 16; break;
        case 1024: swarm->active_tile_w = 64;  swarm->active_tile_h = 16; break;
        case 2048: swarm->active_tile_w = 64;  swarm->active_tile_h = 32; break;
        case 4096: swarm->active_tile_w = 128; swarm->active_tile_h = 32; break;
        default:
            /* For non-standard sizes, compute approximate dimensions */
            swarm->active_tile_w = (swarm->active_tile_count < D8_K_EXPECTED_W) 
                ? swarm->active_tile_count : D8_K_EXPECTED_W;
            swarm->active_tile_h = (swarm->active_tile_count + swarm->active_tile_w - 1) 
                / swarm->active_tile_w;
            break;
        }
    }
}

d8_swarm_t* d8_swarm_get_swarm(d8_swarm_t* swarm) {
    return swarm;
}

d8_tile_params_t d8_swarm_get_tile_params(const d8_swarm_t* swarm, size_t tile_id) {
    d8_tile_params_t params = {0};
    if (!swarm || tile_id >= D8_K_TILE_COUNT) {
        return params;
    }
    params.thr_lo = swarm->thr_lo[tile_id];
    params.thr_hi = swarm->thr_hi[tile_id];
    params.decay16 = swarm->decay16[tile_id];
    params.domain_id = swarm->domain_id[tile_id];
    params.priority = swarm->priority[tile_id];
    params.pattern_id = swarm->pattern_id[tile_id];
    params.reset_on_fire_mask16 = swarm->reset_on_fire_mask16[tile_id];
    return params;
}

d8_status_t d8_swarm_serialize_current_bake(const d8_swarm_t* swarm,
                                             d8_u8** out_blob, size_t* out_size) {
    if (!swarm || !out_blob || !out_size) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "NULL pointer");
    }

    /* Calculate total size - use active_tile_count for serialization */
    /* This ensures bake files contain only the tiles that are actually used */
    size_t tile_count = swarm->active_tile_count;
    if (tile_count == 0 || tile_count > D8_K_TILE_COUNT) {
        tile_count = D8_K_TILE_COUNT;  /* Default to full count */
    }

    size_t header_size = 28;
    size_t topology_size = 8 + 16;  /* TLV header + data */
    size_t tile_params_size = 8 + (tile_count * 13);
    size_t routing_size = 8 + (tile_count * 2);
    size_t weights_size = 8 + (tile_count * 40);
    size_t reset_on_fire_size = 8 + (tile_count * 2);
    size_t readout_size = 8 + 12;
    size_t field_limit_size = 8 + 4;  /* TILE_FIELD_LIMIT TLV */
    size_t crc_size = 8 + 4;
    size_t total_size = header_size + topology_size + tile_params_size +
                        routing_size + weights_size + reset_on_fire_size +
                        readout_size + field_limit_size + crc_size;

    /* Allocate buffer */
    d8_u8* out_buf = (d8_u8*)malloc(total_size);
    if (!out_buf) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "Memory allocation failed");
    }
    memset(out_buf, 0, total_size);

    size_t offset = 0;

    /* Write header */
    d8_bake_header_t header;
    memcpy(header.magic, "D8BK", 4);
    header.ver_major = D8_BAKE_VER_MAJOR;
    header.ver_minor = D8_BAKE_VER_MINOR;
    header.flags = swarm->bake_flags;
    header.total_len = 0;
    header.bake_id = swarm->bake_id;
    header.profile_id = swarm->profile_id;
    header.reserved0 = 0;
    memcpy(out_buf + offset, &header, 28);
    offset += 28;

    /* Write TOPOLOGY TLV */
    {
        d8_u8 tlv[24];
        memset(tlv, 0, sizeof(tlv));
        d8_u16* type = (d8_u16*)(tlv + 0);
        d8_u16* tflags = (d8_u16*)(tlv + 2);
        d8_u32* len = (d8_u32*)(tlv + 4);
        *type = D8_TLV_TOPOLOGY;
        *tflags = 0;
        *len = 16;

        d8_u8* data = tlv + 8;
        /* Write tile_count (active_tile_count) to topology */
        data[0] = tile_count & 0xFF;
        data[1] = (tile_count >> 8) & 0xFF;
        data[2] = (tile_count >> 16) & 0xFF;
        data[3] = (tile_count >> 24) & 0xFF;
        data[4] = swarm->topo.tile_w & 0xFF;
        data[5] = (swarm->topo.tile_w >> 8) & 0xFF;
        data[6] = swarm->topo.tile_h & 0xFF;
        data[7] = (swarm->topo.tile_h >> 8) & 0xFF;
        data[8] = swarm->topo.lanes;
        data[9] = swarm->topo.domains;

        /* reserved (bytes 10-11) must be 0 */
        data[10] = 0;
        data[11] = 0;

        /* reserved2 (bytes 12-15) = tile_field_limit for serialization */
        /* This ensures that small swarms can be properly restored */
        d8_u32 tile_field_limit = (d8_u32)swarm->active_tile_count;
        data[12] = tile_field_limit & 0xFF;
        data[13] = (tile_field_limit >> 8) & 0xFF;
        data[14] = (tile_field_limit >> 16) & 0xFF;
        data[15] = (tile_field_limit >> 24) & 0xFF;

        memcpy(out_buf + offset, tlv, sizeof(tlv));
        offset += sizeof(tlv);
    }

    /* Write TILE_PARAMS_V2 TLV */
    {
        d8_u8 tlv_hdr[8];
        memset(tlv_hdr, 0, sizeof(tlv_hdr));
        d8_u16* type = (d8_u16*)(tlv_hdr + 0);
        d8_u16* tflags = (d8_u16*)(tlv_hdr + 2);
        d8_u32* len = (d8_u32*)(tlv_hdr + 4);
        *type = D8_TLV_TILE_PARAMS_V2;
        *tflags = 0;
        *len = (d8_u32)(tile_count * 13);
        memcpy(out_buf + offset, tlv_hdr, 8);
        offset += 8;

        for (size_t t = 0; t < tile_count; t++) {
            d8_u8* p = out_buf + offset;
            d8_i16 thr_lo_val = swarm->thr_lo[t];
            d8_i16 thr_hi_val = swarm->thr_hi[t];

            p[0] = thr_lo_val & 0xFF;
            p[1] = (thr_lo_val >> 8) & 0xFF;
            p[2] = thr_hi_val & 0xFF;
            p[3] = (thr_hi_val >> 8) & 0xFF;
            p[4] = swarm->decay16[t] & 0xFF;
            p[5] = (swarm->decay16[t] >> 8) & 0xFF;
            p[6] = swarm->domain_id[t] & 0x0F;
            p[7] = swarm->priority[t];
            p[8] = swarm->pattern_id[t] & 0xFF;
            p[9] = (swarm->pattern_id[t] >> 8) & 0xFF;
            p[10] = swarm->reset_on_fire_mask16[t] & 0xFF;
            p[11] = (swarm->reset_on_fire_mask16[t] >> 8) & 0xFF;
            p[12] = 0;  /* Reserved */
            offset += 13;
        }
    }

    /* Write ROUTING_FLAGS TLV */
    {
        d8_u8 tlv_hdr[8];
        memset(tlv_hdr, 0, sizeof(tlv_hdr));
        d8_u16* type = (d8_u16*)(tlv_hdr + 0);
        d8_u16* tflags = (d8_u16*)(tlv_hdr + 2);
        d8_u32* len = (d8_u32*)(tlv_hdr + 4);
        *type = D8_TLV_TILE_ROUTING_FLAGS16;
        *tflags = 0;
        *len = (d8_u32)(tile_count * 2);
        memcpy(out_buf + offset, tlv_hdr, 8);
        offset += 8;

        for (size_t t = 0; t < tile_count; t++) {
            d8_u16 flags = 0;
            if (swarm->maskN[t]) flags |= 0x01;
            if (swarm->maskE[t]) flags |= 0x02;
            if (swarm->maskS[t]) flags |= 0x04;
            if (swarm->maskW[t]) flags |= 0x08;
            if (swarm->maskNE[t]) flags |= 0x10;
            if (swarm->maskSE[t]) flags |= 0x20;
            if (swarm->maskSW[t]) flags |= 0x40;
            if (swarm->maskNW[t]) flags |= 0x80;
            if (swarm->bus_r[t]) flags |= 0x0100;
            if (swarm->bus_w[t]) flags |= 0x0200;
            out_buf[offset++] = flags & 0xFF;
            out_buf[offset++] = (flags >> 8) & 0xFF;
        }
    }

    /* Write WEIGHTS_PACKED TLV */
    {
        d8_u8 tlv_hdr[8];
        memset(tlv_hdr, 0, sizeof(tlv_hdr));
        d8_u16* type = (d8_u16*)(tlv_hdr + 0);
        d8_u16* tflags = (d8_u16*)(tlv_hdr + 2);
        d8_u32* len = (d8_u32*)(tlv_hdr + 4);
        *type = D8_TLV_TILE_WEIGHTS_PACKED;
        *tflags = 0;
        *len = (d8_u32)(tile_count * 40);
        memcpy(out_buf + offset, tlv_hdr, 8);
        offset += 8;

        for (size_t t = 0; t < tile_count; t++) {
            /* Pack 64 nibbles into 32 bytes */
            for (size_t i = 0; i < 32; i++) {
                d8_u8 lo = swarm->w_mag[t * 64 + i * 2] & 0x0F;
                d8_u8 hi = swarm->w_mag[t * 64 + i * 2 + 1] & 0x0F;
                out_buf[offset++] = (hi << 4) | lo;
            }
            /* Pack 64 bits into 8 bytes */
            for (size_t i = 0; i < 8; i++) {
                d8_u8 byte = 0;
                for (size_t bit = 0; bit < 8; bit++) {
                    byte |= (swarm->w_sign[t * 64 + i * 8 + bit] & 1) << bit;
                }
                out_buf[offset++] = byte;
            }
        }
    }

    /* Write RESET_ON_FIRE_MASK16 TLV */
    {
        d8_u8 tlv_hdr[8];
        memset(tlv_hdr, 0, sizeof(tlv_hdr));
        d8_u16* type = (d8_u16*)(tlv_hdr + 0);
        d8_u16* tflags = (d8_u16*)(tlv_hdr + 2);
        d8_u32* len = (d8_u32*)(tlv_hdr + 4);
        *type = D8_TLV_RESET_ON_FIRE_MASK16;
        *tflags = 0;
        *len = (d8_u32)(tile_count * 2);
        memcpy(out_buf + offset, tlv_hdr, 8);
        offset += 8;

        for (size_t t = 0; t < tile_count; t++) {
            out_buf[offset++] = swarm->reset_on_fire_mask16[t] & 0xFF;
            out_buf[offset++] = (swarm->reset_on_fire_mask16[t] >> 8) & 0xFF;
        }
    }

    /* Write READOUT_POLICY TLV */
    {
        d8_u8 tlv[20];
        memset(tlv, 0, sizeof(tlv));
        d8_u16* type = (d8_u16*)(tlv + 0);
        d8_u16* tflags = (d8_u16*)(tlv + 2);
        d8_u32* len = (d8_u32*)(tlv + 4);
        *type = D8_TLV_READOUT_POLICY;
        *tflags = 0;
        *len = 12;

        d8_u8* data = tlv + 8;
        data[0] = swarm->readout.mode;
        data[1] = 0;  /* reserved */
        data[2] = swarm->readout.winner_domain_mask & 0xFF;
        data[3] = (swarm->readout.winner_domain_mask >> 8) & 0xFF;
        data[4] = swarm->readout.settle_ns & 0xFF;
        data[5] = (swarm->readout.settle_ns >> 8) & 0xFF;
        data[6] = swarm->readout.reserved1 & 0xFF;
        data[7] = (swarm->readout.reserved1 >> 8) & 0xFF;
        data[8] = swarm->readout.reserved2 & 0xFF;
        data[9] = (swarm->readout.reserved2 >> 8) & 0xFF;
        data[10] = (swarm->readout.reserved2 >> 16) & 0xFF;
        data[11] = (swarm->readout.reserved2 >> 24) & 0xFF;

        memcpy(out_buf + offset, tlv, sizeof(tlv));
        offset += sizeof(tlv);
    }

    /* Write TILE_FIELD_LIMIT TLV - stores active_tile_count for proper restoration */
    {
        d8_u8 tlv[12];
        memset(tlv, 0, sizeof(tlv));
        d8_u16* type = (d8_u16*)(tlv + 0);
        d8_u16* tflags = (d8_u16*)(tlv + 2);
        d8_u32* len = (d8_u32*)(tlv + 4);
        *type = D8_TLV_TILE_FIELD_LIMIT;
        *tflags = 0;
        *len = 4;

        d8_u32 tile_field_limit = (d8_u32)swarm->active_tile_count;
        tlv[8] = tile_field_limit & 0xFF;
        tlv[9] = (tile_field_limit >> 8) & 0xFF;
        tlv[10] = (tile_field_limit >> 16) & 0xFF;
        tlv[11] = (tile_field_limit >> 24) & 0xFF;

        memcpy(out_buf + offset, tlv, sizeof(tlv));
        offset += sizeof(tlv);
    }

    /* Write total_len BEFORE CRC (total_len is part of CRC calculation) */
    /* total_len = full blob size including CRC TLV (12 bytes) */
    d8_u32 total_len_value = (d8_u32)(offset + 12);  /* +12 for CRC TLV */
    memcpy(out_buf + 12, &total_len_value, 4);  /* offset 12 = header.total_len */

    /* Calculate CRC32 (includes total_len, excludes CRC TLV) */
    d8_u32 crc = d8_crc32_ieee(out_buf, offset);
    {
        d8_u8 tlv[12];
        memset(tlv, 0, sizeof(tlv));
        d8_u16* type = (d8_u16*)(tlv + 0);
        d8_u16* tflags = (d8_u16*)(tlv + 2);
        d8_u32* len = (d8_u32*)(tlv + 4);
        *type = D8_TLV_CRC32;
        *tflags = 0;
        *len = 4;
        tlv[8] = crc & 0xFF;
        tlv[9] = (crc >> 8) & 0xFF;
        tlv[10] = (crc >> 16) & 0xFF;
        tlv[11] = (crc >> 24) & 0xFF;
        memcpy(out_buf + offset, tlv, sizeof(tlv));
        offset += sizeof(tlv);
    }

    /* total_len already written correctly before CRC */

    *out_blob = out_buf;
    *out_size = offset;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

#ifdef __cplusplus
}
#endif
