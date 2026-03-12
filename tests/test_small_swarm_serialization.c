/*
 * DECIMA-8 Core - Small Swarm Serialization Test
 *
 * Tests serialization/deserialization of small swarms (e.g., 256 tiles)
 * to ensure all tiles are properly saved and restored.
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include "d8/swarm_core.h"
#include "bake_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define TEST_PASSED() printf("  PASSED\n")
#define TEST_FAILED(msg) do { printf("  FAILED: %s\n", msg); return 1; } while(0)

int test_small_swarm_serialize_256(void) {
    printf("Test 1: Serialize/deserialize 256-tile swarm...\n");
    
    /* Generate bake with 256 tiles */
    d8_u8 bake_blob[32000];
    size_t bake_size = 0;
    
    if (d8_bake_gen_custom(bake_blob, &bake_size, 256, 100, 200, 10) != 0) {
        TEST_FAILED("Failed to generate bake");
    }
    printf("  Generated bake: %zu bytes\n", bake_size);
    
    /* Create swarm core and apply bake */
    d8_swarm_core_t* core1 = d8_swarm_core_create();
    if (!core1) {
        TEST_FAILED("Failed to create core1");
    }
    
    d8_status_t st = d8_swarm_core_ev_bake(core1, bake_blob, bake_size);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Failed to apply bake");
    }
    
    /* Verify active_tile_count */
    size_t active_count = d8_swarm_core_active_tile_count(core1);
    printf("  Active tile count: %zu\n", active_count);
    if (active_count != 256) {
        printf("  Expected 256, got %zu\n", active_count);
        d8_swarm_core_destroy(core1);
        TEST_FAILED("active_tile_count mismatch after bake");
    }
    
    /* Set unique values for last 6 tiles (250-255) */
    for (size_t t = 250; t < 256; t++) {
        d8_swarm_core_set_tile_params(core1, t, 
            (d8_i16)(t + 100),    /* thr_lo */
            (d8_i16)(t + 200),    /* thr_hi */
            (d8_u16)(t + 10),     /* decay16 */
            (d8_u8)(t % 16),      /* domain_id */
            (d8_u8)(t % 8)        /* priority */
        );
        d8_swarm_core_set_tile_pattern_id(core1, t, (d8_u16)(t + 1000));
    }
    
    /* Serialize */
    d8_u8* blob = NULL;
    size_t blob_size = 0;
    st = d8_swarm_core_serialize_bake(core1, &blob, &blob_size);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Serialization failed");
    }
    printf("  Serialized to %zu bytes\n", blob_size);
    
    /* Create new core and deserialize */
    d8_swarm_core_t* core2 = d8_swarm_core_create();
    if (!core2) {
        d8_swarm_core_free_bake_blob(blob);
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Failed to create core2");
    }
    
    st = d8_swarm_core_ev_bake(core2, blob, blob_size);
    d8_swarm_core_free_bake_blob(blob);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        d8_swarm_core_destroy(core2);
        TEST_FAILED("Deserialization failed");
    }
    
    /* Verify active_tile_count */
    active_count = d8_swarm_core_active_tile_count(core2);
    if (active_count != 256) {
        printf("  active_tile_count: expected 256, got %zu\n", active_count);
        d8_swarm_core_destroy(core1);
        d8_swarm_core_destroy(core2);
        TEST_FAILED("active_tile_count mismatch after deserialize");
    }
    
    /* Verify last 6 tiles have correct data */
    for (size_t t = 250; t < 256; t++) {
        d8_tile_params_t params = d8_swarm_core_get_tile_params(core2, t);
        if (params.thr_lo != (d8_i16)(t + 100)) {
            printf("  Tile %zu: thr_lo mismatch (expected %d, got %d)\n", 
                   t, (int)(t + 100), (int)params.thr_lo);
            d8_swarm_core_destroy(core1);
            d8_swarm_core_destroy(core2);
            TEST_FAILED("thr_lo mismatch");
        }
        if (params.thr_hi != (d8_i16)(t + 200)) {
            printf("  Tile %zu: thr_hi mismatch (expected %d, got %d)\n", 
                   t, (int)(t + 200), (int)params.thr_hi);
            d8_swarm_core_destroy(core1);
            d8_swarm_core_destroy(core2);
            TEST_FAILED("thr_hi mismatch");
        }
        if (params.pattern_id != (d8_u16)(t + 1000)) {
            printf("  Tile %zu: pattern_id mismatch (expected %u, got %u)\n", 
                   t, (unsigned int)(t + 1000), (unsigned int)params.pattern_id);
            d8_swarm_core_destroy(core1);
            d8_swarm_core_destroy(core2);
            TEST_FAILED("pattern_id mismatch");
        }
    }
    
    d8_swarm_core_destroy(core1);
    d8_swarm_core_destroy(core2);
    TEST_PASSED();
    return 0;
}

int test_small_swarm_serialize_64(void) {
    printf("Test 2: Serialize/deserialize 64-tile swarm (8x8)...\n");
    
    /* Generate bake with 64 tiles */
    d8_u8 bake_blob[16000];
    size_t bake_size = 0;
    
    if (d8_bake_gen_custom(bake_blob, &bake_size, 64, 50, 150, 5) != 0) {
        TEST_FAILED("Failed to generate bake");
    }
    printf("  Generated bake: %zu bytes\n", bake_size);
    
    /* Create swarm core and apply bake */
    d8_swarm_core_t* core1 = d8_swarm_core_create();
    if (!core1) {
        TEST_FAILED("Failed to create core1");
    }
    
    d8_status_t st = d8_swarm_core_ev_bake(core1, bake_blob, bake_size);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Failed to apply bake");
    }
    
    /* Verify active_tile_count */
    size_t active_count = d8_swarm_core_active_tile_count(core1);
    printf("  Active tile count: %zu\n", active_count);
    if (active_count != 64) {
        printf("  Expected 64, got %zu\n", active_count);
        d8_swarm_core_destroy(core1);
        TEST_FAILED("active_tile_count mismatch after bake");
    }
    
    /* Set unique values for last 4 tiles (60-63) */
    for (size_t t = 60; t < 64; t++) {
        d8_swarm_core_set_tile_params(core1, t, 
            (d8_i16)(t * 2),      /* thr_lo */
            (d8_i16)(t * 3),      /* thr_hi */
            (d8_u16)(t + 5),      /* decay16 */
            (d8_u8)(t % 16),      /* domain_id */
            (d8_u8)(t % 4)        /* priority */
        );
        d8_swarm_core_set_tile_pattern_id(core1, t, (d8_u16)(t * 10));
    }
    
    /* Serialize */
    d8_u8* blob = NULL;
    size_t blob_size = 0;
    st = d8_swarm_core_serialize_bake(core1, &blob, &blob_size);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Serialization failed");
    }
    printf("  Serialized to %zu bytes\n", blob_size);
    
    /* Create new core and deserialize */
    d8_swarm_core_t* core2 = d8_swarm_core_create();
    if (!core2) {
        d8_swarm_core_free_bake_blob(blob);
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Failed to create core2");
    }
    
    st = d8_swarm_core_ev_bake(core2, blob, blob_size);
    d8_swarm_core_free_bake_blob(blob);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        d8_swarm_core_destroy(core2);
        TEST_FAILED("Deserialization failed");
    }
    
    /* Verify active_tile_count */
    active_count = d8_swarm_core_active_tile_count(core2);
    if (active_count != 64) {
        printf("  active_tile_count: expected 64, got %zu\n", active_count);
        d8_swarm_core_destroy(core1);
        d8_swarm_core_destroy(core2);
        TEST_FAILED("active_tile_count mismatch after deserialize");
    }
    
    /* Verify last 4 tiles have correct data */
    for (size_t t = 60; t < 64; t++) {
        d8_tile_params_t params = d8_swarm_core_get_tile_params(core2, t);
        if (params.thr_lo != (d8_i16)(t * 2)) {
            printf("  Tile %zu: thr_lo mismatch (expected %d, got %d)\n", 
                   t, (int)(t * 2), (int)params.thr_lo);
            d8_swarm_core_destroy(core1);
            d8_swarm_core_destroy(core2);
            TEST_FAILED("thr_lo mismatch");
        }
        if (params.thr_hi != (d8_i16)(t * 3)) {
            printf("  Tile %zu: thr_hi mismatch (expected %d, got %d)\n", 
                   t, (int)(t * 3), (int)params.thr_hi);
            d8_swarm_core_destroy(core1);
            d8_swarm_core_destroy(core2);
            TEST_FAILED("thr_hi mismatch");
        }
    }
    
    d8_swarm_core_destroy(core1);
    d8_swarm_core_destroy(core2);
    TEST_PASSED();
    return 0;
}

int test_roundtrip_with_flash(void) {
    printf("Test 3: Roundtrip with flash operation...\n");
    
    /* Generate bake with 128 tiles */
    d8_u8 bake_blob[20000];
    size_t bake_size = 0;
    
    if (d8_bake_gen_custom(bake_blob, &bake_size, 128, -100, 100, 5) != 0) {
        TEST_FAILED("Failed to generate bake");
    }
    printf("  Generated bake: %zu bytes\n", bake_size);
    
    /* Create swarm core and apply bake */
    d8_swarm_core_t* core1 = d8_swarm_core_create();
    if (!core1) {
        TEST_FAILED("Failed to create core1");
    }
    
    d8_status_t st = d8_swarm_core_ev_bake(core1, bake_blob, bake_size);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Failed to apply bake");
    }
    
    /* Configure tile 0 with BUS_R for flash */
    d8_tile_routing_masks_t masks = {0};
    masks.bus_r = 1;
    d8_swarm_core_set_tile_routing_masks(core1, 0, &masks);
    
    /* Serialize */
    d8_u8* blob = NULL;
    size_t blob_size = 0;
    st = d8_swarm_core_serialize_bake(core1, &blob, &blob_size);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Serialization failed");
    }
    
    /* Create core2 and load bake */
    d8_swarm_core_t* core2 = d8_swarm_core_create();
    if (!core2) {
        d8_swarm_core_free_bake_blob(blob);
        d8_swarm_core_destroy(core1);
        TEST_FAILED("Failed to create core2");
    }
    
    st = d8_swarm_core_ev_bake(core2, blob, blob_size);
    d8_swarm_core_free_bake_blob(blob);
    if (!d8_status_is_ok(&st)) {
        d8_swarm_core_destroy(core1);
        d8_swarm_core_destroy(core2);
        TEST_FAILED("Deserialization failed");
    }
    
    /* Run flash on core2 */
    d8_u8 vsb[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    d8_flash_result_t result = d8_swarm_core_flash(core2, 1, vsb);
    if (!d8_status_is_ok(&result.st)) {
        printf("  Flash failed: %s\n", result.st.msg);
        d8_swarm_core_destroy(core1);
        d8_swarm_core_destroy(core2);
        TEST_FAILED("Flash operation failed");
    }
    
    /* Verify flash completed successfully */
    printf("  Flash completed successfully\n");
    
    d8_swarm_core_destroy(core1);
    d8_swarm_core_destroy(core2);
    TEST_PASSED();
    return 0;
}

int main(void) {
    printf("========================================\n");
    printf("DECIMA-8 Small Swarm Serialization Test\n");
    printf("========================================\n\n");
    
    int failures = 0;
    
    failures += test_small_swarm_serialize_256();
    failures += test_small_swarm_serialize_64();
    failures += test_roundtrip_with_flash();
    
    printf("\n========================================\n");
    if (failures == 0) {
        printf("All tests passed!\n");
    } else {
        printf("FAILED: %d test(s) failed\n", failures);
    }
    printf("========================================\n");
    
    return failures;
}
