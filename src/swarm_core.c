/*
 * DECIMA-8 Core - Pure C Implementation
 * Swarm Core Implementation (unified interface for IDE)
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/swarm_core.h"
#include "d8/swarm.h"
#include "d8/bake.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Internal structure
 * ============================================================================ */

struct d8_swarm_core {
    d8_swarm_t*         swarm;
    d8_bake_view_t*     bake_view;  /* Pointer to avoid stack overflow */
    d8_flash_result_t   last_flash_result;
    bool                is_baked;
    bool                double_strait;
};

/* ============================================================================
 * Creation / Destruction
 * ============================================================================ */

d8_swarm_core_t* d8_swarm_core_create(void) {
    d8_swarm_core_t* core = (d8_swarm_core_t*)calloc(1, sizeof(d8_swarm_core_t));
    if (!core) return NULL;

    core->swarm = d8_swarm_create();
    if (!core->swarm) {
        free(core);
        return NULL;
    }

    /* Allocate bake_view on heap to avoid stack overflow */
    core->bake_view = (d8_bake_view_t*)calloc(1, sizeof(d8_bake_view_t));
    if (!core->bake_view) {
        d8_swarm_destroy(core->swarm);
        free(core);
        return NULL;
    }

    return core;
}

void d8_swarm_core_destroy(d8_swarm_core_t* core) {
    if (!core) return;

    if (core->bake_view) {
        free(core->bake_view);
        core->bake_view = NULL;
    }
    if (core->swarm) {
        d8_swarm_destroy(core->swarm);
    }
    free(core);
}

/* ============================================================================
 * Bake operations
 * ============================================================================ */

d8_status_t d8_swarm_core_ev_bake(d8_swarm_core_t* core, const d8_u8* blob, size_t blob_size) {
    if (!core || !blob || blob_size == 0) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "NULL pointer");
    }

    /* Parse bake blob */
    d8_status_t st = d8_bake_validate(blob, blob_size, core->bake_view);
    if (!d8_status_is_ok(&st)) {
        return st;
    }
    
    /* If bake_id is 0, set it to 1 so Conductor knows bake is active */
    if (core->bake_view->bake_id == 0) {
        core->bake_view->bake_id = 1;
    }
    
    /* Apply bake to swarm */
    st = d8_swarm_ev_bake(core->swarm, blob, blob_size);
    if (!d8_status_is_ok(&st)) {
        return st;
    }
    
    /* Update snapshot bake_id_active from bake_view */
    d8_swarm_set_snapshot_bake_id(core->swarm, core->bake_view->bake_id, core->bake_view->profile_id);

    /* Also update shared_buffer if it's set */
    d8_swarm_update_shared_buffer_bake_id(core->swarm, core->bake_view->bake_id, core->bake_view->profile_id);
    
    /* Set bake_id in swarm (override parsed value if it was 0) */
    d8_swarm_set_bake_id(core->swarm, core->bake_view->bake_id, core->bake_view->profile_id);
    
    core->is_baked = true;
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

bool d8_swarm_core_is_baked(const d8_swarm_core_t* core) {
    return core && core->is_baked;
}

d8_status_t d8_swarm_core_serialize_bake(const d8_swarm_core_t* core,
                                          d8_u8** out_blob, size_t* out_size) {
    if (!core || !out_blob || !out_size) {
        return D8_STATUS_MAKE(D8_STATUS_BAKE_NO_BLOB, 0, "NULL pointer");
    }

    if (!core->is_baked || !core->swarm) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "Not baked");
    }

    /* Use swarm serialization directly */
    return d8_swarm_serialize_current_bake(core->swarm, out_blob, out_size);
}

void d8_swarm_core_free_bake_blob(d8_u8* blob) {
    if (blob) {
        free(blob);
    }
}

/* ============================================================================
 * Flash operations
 * ============================================================================ */

d8_flash_result_t d8_swarm_core_flash(d8_swarm_core_t* core, d8_u32 tag,
                                       const d8_u8 ingress[D8_K_LANES]) {
    d8_flash_result_t empty = {0};

    if (!core || !core->is_baked || !core->swarm || !ingress) {
        empty.st = D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or not baked");
        return empty;
    }

    /* Execute flash */
    core->last_flash_result = d8_swarm_ev_flash(core->swarm, tag, ingress);
    return core->last_flash_result;
}

void d8_swarm_core_set_vsb_ingress(d8_swarm_core_t* core, const d8_u8 ingress[D8_K_LANES]) {
    if (!core || !core->swarm || !ingress) return;
    /* VSB ingress is set during flash, this is a no-op for now */
    (void)ingress;
}

d8_flash_result_t d8_swarm_core_get_last_flash_result(const d8_swarm_core_t* core) {
    d8_flash_result_t empty = {0};
    if (!core) return empty;
    return core->last_flash_result;
}

/* ============================================================================
 * Reset operations
 * ============================================================================ */

d8_status_t d8_swarm_core_ev_reset_domain(d8_swarm_core_t* core, d8_u16 mask16) {
    if (!core || !core->swarm) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL");
    }
    return d8_swarm_ev_reset_domain(core->swarm, mask16);
}

/* ============================================================================
 * Telemetry / Snapshot
 * ============================================================================ */

const d8_view_snapshot_t* d8_swarm_core_get_snapshot(const d8_swarm_core_t* core) {
    if (!core || !core->swarm) return NULL;
    return d8_swarm_get_snapshot(core->swarm);
}

size_t d8_swarm_core_active_tile_count(const d8_swarm_core_t* core) {
    if (!core || !core->swarm) return 0;
    return d8_swarm_get_active_tile_count(core->swarm);
}

void d8_swarm_core_set_shared_buffer(d8_swarm_core_t* core, d8_shared_buffer_t* buffer) {
    if (!core || !core->swarm) return;
    d8_swarm_set_shared_buffer(core->swarm, buffer);
}

/* ============================================================================
 * Tile queries (for IDE)
 * ============================================================================ */

bool d8_swarm_core_get_tile_locked(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return false;
    return d8_swarm_get_tile_locked(core->swarm, tile_id);
}

d8_i16 d8_swarm_core_get_tile_thr_cur(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_thr_cur(core->swarm, tile_id);
}

d8_u16 d8_swarm_core_get_tile_pattern_id(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_pattern_id(core->swarm, tile_id);
}

d8_tile_routing_masks_t d8_swarm_core_get_tile_routing_masks(const d8_swarm_core_t* core, size_t tile_id) {
    d8_tile_routing_masks_t masks = {0};
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return masks;
    return d8_swarm_get_tile_routing_masks(core->swarm, tile_id);
}

/* ============================================================================
 * Tile modification (for IDE)
 * ============================================================================ */

d8_status_t d8_swarm_core_set_tile_locked(d8_swarm_core_t* core, size_t tile_id, bool locked) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    return d8_swarm_set_tile_locked(core->swarm, tile_id, locked);
}

d8_status_t d8_swarm_core_set_tile_weight_sign(d8_swarm_core_t* core, size_t tile_id,
                                                size_t weight_idx, bool sign) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad index");
    }
    return d8_swarm_set_tile_weight_sign(core->swarm, tile_id, weight_idx, sign);
}

d8_status_t d8_swarm_core_set_tile_weight_mag(d8_swarm_core_t* core, size_t tile_id,
                                               size_t weight_idx, d8_u8 mag) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad index");
    }
    return d8_swarm_set_tile_weight_mag(core->swarm, tile_id, weight_idx, mag);
}

d8_status_t d8_swarm_core_set_tile_routing_masks(d8_swarm_core_t* core, size_t tile_id,
                                                  const d8_tile_routing_masks_t* masks) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT || !masks) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    
    d8_status_t result = d8_swarm_set_tile_routing_masks(core->swarm, tile_id,
        masks->maskN, masks->maskE, masks->maskS, masks->maskW,
        masks->maskNE, masks->maskSE, masks->maskSW, masks->maskNW,
        masks->bus_w, masks->bus_r);
    
    /* Memory barrier to ensure writes are visible to flash */
#ifdef _WIN32
    _mm_mfence();
#endif
    
    return result;
}

d8_status_t d8_swarm_core_set_tile_reset_on_fire_mask(d8_swarm_core_t* core, size_t tile_id,
                                                       d8_u16 mask16) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    return d8_swarm_set_tile_reset_on_fire_mask(core->swarm, tile_id, mask16);
}

d8_status_t d8_swarm_core_set_tile_params(d8_swarm_core_t* core, size_t tile_id,
                                           d8_i16 thr_lo, d8_i16 thr_hi,
                                           d8_u16 decay16, d8_u8 domain_id, d8_u8 priority) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    /* Use swarm API functions for field access */
    d8_swarm_set_tile_params_direct(core->swarm, tile_id, thr_lo, thr_hi, decay16, domain_id, priority);
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_status_t d8_swarm_core_set_tile_pattern_id(d8_swarm_core_t* core, size_t tile_id, d8_u16 pattern_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or bad tile_id");
    }
    d8_swarm_set_tile_pattern_id_direct(core->swarm, tile_id, pattern_id);
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_i16 d8_swarm_core_get_tile_thr_lo(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_thr_lo_direct(core->swarm, tile_id);
}

d8_i16 d8_swarm_core_get_tile_thr_hi(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_thr_hi_direct(core->swarm, tile_id);
}

d8_u16 d8_swarm_core_get_tile_decay16(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_decay16_direct(core->swarm, tile_id);
}

d8_u8 d8_swarm_core_get_tile_domain_id(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_domain_id_direct(core->swarm, tile_id);
}

d8_u8 d8_swarm_core_get_tile_priority(const d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) return 0;
    return d8_swarm_get_tile_priority_direct(core->swarm, tile_id);
}

bool d8_swarm_core_get_tile_weight_sign(const d8_swarm_core_t* core, size_t tile_id, size_t weight_idx) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) return false;
    return d8_swarm_get_tile_weight_sign_direct(core->swarm, tile_id, weight_idx);
}

d8_u8 d8_swarm_core_get_tile_weight_mag(const d8_swarm_core_t* core, size_t tile_id, size_t weight_idx) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT || weight_idx >= 64) return 0;
    return d8_swarm_get_tile_weight_mag_direct(core->swarm, tile_id, weight_idx);
}

d8_status_t d8_swarm_core_set_tile_field_limit(d8_swarm_core_t* core, d8_u32 limit) {
    if (!core || !core->swarm) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or not baked");
    }
    /* Note: C API stores limit but doesn't trigger re-bake automatically */
    d8_swarm_set_tile_field_limit_direct(core->swarm, limit);
    return D8_STATUS_MAKE(D8_STATUS_OK, 0, "OK");
}

d8_swarm_t* d8_swarm_core_get_swarm(d8_swarm_core_t* core) {
    if (!core) return NULL;
    return core->swarm;
}

d8_status_t d8_swarm_core_serialize_current_bake(d8_swarm_core_t* core,
                                                  d8_u8** out_blob, size_t* out_size) {
    if (!core || !core->swarm) {
        return D8_STATUS_MAKE(D8_STATUS_NOT_BAKED, 0, "NULL or not baked");
    }
    return d8_swarm_serialize_current_bake(core->swarm, out_blob, out_size);
}

d8_tile_params_t d8_swarm_core_get_tile_params(d8_swarm_core_t* core, size_t tile_id) {
    if (!core || !core->swarm || tile_id >= D8_K_TILE_COUNT) {
        d8_tile_params_t empty = {0};
        return empty;
    }
    /* Use swarm API functions for field access */
    d8_tile_params_t params;
    params.thr_lo = d8_swarm_get_tile_thr_lo_direct(core->swarm, tile_id);
    params.thr_hi = d8_swarm_get_tile_thr_hi_direct(core->swarm, tile_id);
    params.decay16 = d8_swarm_get_tile_decay16_direct(core->swarm, tile_id);
    params.domain_id = d8_swarm_get_tile_domain_id_direct(core->swarm, tile_id);
    params.priority = d8_swarm_get_tile_priority_direct(core->swarm, tile_id);
    params.pattern_id = d8_swarm_get_tile_pattern_id_direct(core->swarm, tile_id);
    params.reset_on_fire_mask16 = d8_swarm_get_tile_reset_on_fire_mask_direct(core->swarm, tile_id);
    return params;
}

/* ============================================================================
 * Double-strait bake (for cascade)
 * ============================================================================ */

bool d8_swarm_core_double_strait_bake(const d8_swarm_core_t* core) {
    return core && core->double_strait;
}

void d8_swarm_core_set_double_strait_bake(d8_swarm_core_t* core, bool enable) {
    if (!core) return;
    core->double_strait = enable;
}

#ifdef __cplusplus
}
#endif

