/*
 * DECIMA-8 Flash Test
 * 
 * All rights belong to the ORDEN (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "d8/swarm.h"
#include "bake_gen.h"

static int expect(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "EXPECT failed: %s\n", msg);
        return 1;
    }
    return 0;
}

static int bake_swarm(d8_swarm_t* swarm, int16_t thr_lo, int16_t thr_hi) {
    uint8_t bake_data[240000];
    size_t bake_size = 0;
    if (d8_bake_gen_custom(bake_data, &bake_size, 256, thr_lo, thr_hi, 0) != 0) {
        return -1;
    }
    d8_status_t st = d8_swarm_ev_bake(swarm, bake_data, bake_size);
    return st.code == D8_STATUS_OK ? 0 : -1;
}

static void set_all_weights(d8_swarm_t* swarm, size_t tile_id, uint8_t mag) {
    for (size_t i = 0; i < 64; i++) {
        d8_swarm_set_tile_weight_sign(swarm, tile_id, i, true);
        d8_swarm_set_tile_weight_mag(swarm, tile_id, i, mag);
    }
}

static int test_strict_ingress(void) {
    d8_swarm_t* swarm = d8_swarm_create();
    if (expect(swarm != NULL, "create swarm")) return 1;
    if (expect(bake_swarm(swarm, 1, 100) == 0, "bake strict ingress swarm")) return 1;

    uint8_t ingress[8] = {16, 0, 0, 0, 0, 0, 0, 0};
    d8_flash_result_t result = d8_swarm_ev_flash(swarm, 1, ingress);
    int failed = expect(result.st.code == D8_STATUS_BAD_INGRESS_LEVEL, "ingress > 15 must fail");
    d8_swarm_destroy(swarm);
    return failed;
}

static int test_winner_collide_autoreset(void) {
    d8_swarm_t* swarm = d8_swarm_create();
    if (expect(swarm != NULL, "create swarm")) return 1;
    if (expect(bake_swarm(swarm, 1, 1000) == 0, "bake collision swarm")) return 1;

    d8_swarm_set_tile_params_direct(swarm, 0, 1, 1000, 0, 0, 1);
    d8_swarm_set_tile_params_direct(swarm, 1, 1, 1000, 0, 0, 2);
    d8_swarm_set_tile_pattern_id_direct(swarm, 0, 10);
    d8_swarm_set_tile_pattern_id_direct(swarm, 1, 11);
    d8_swarm_set_tile_routing_masks(swarm, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1);
    d8_swarm_set_tile_routing_masks(swarm, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1);
    d8_swarm_set_tile_reset_on_fire_mask(swarm, 1, 0x0001);
    set_all_weights(swarm, 0, 7);
    set_all_weights(swarm, 1, 7);

    uint8_t ingress[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    d8_flash_result_t result = d8_swarm_ev_flash(swarm, 10, ingress);
    const d8_view_snapshot_t* snap = d8_swarm_get_snapshot(swarm);

    int failed = 0;
    failed |= expect(result.st.code == D8_STATUS_OK, "collision flash must succeed");
    failed |= expect((snap->collide_mask16 & 0x0001) != 0, "domain 0 collision must be reported");
    failed |= expect(snap->winner_tile_id[0] == 1, "higher priority tile must win");
    failed |= expect(snap->winner_pattern_id[0] == 11, "winner pattern id must match");
    failed |= expect(snap->fired_cnt_sat[0] == 2, "two fired tiles must be counted");
    failed |= expect(snap->auto_reset_mask16 == 0x0001, "winner reset mask must be exported");
    failed |= expect(!d8_swarm_get_tile_locked(swarm, 0), "auto-reset must clear non-winner in reset domain");
    failed |= expect(d8_swarm_get_tile_locked(swarm, 1), "auto-reset must keep winner locked");

    d8_swarm_destroy(swarm);
    return failed ? 1 : 0;
}

static int test_locked_ancestor_write(void) {
    d8_swarm_t* swarm = d8_swarm_create();
    if (expect(swarm != NULL, "create swarm")) return 1;
    if (expect(bake_swarm(swarm, 1, 1000) == 0, "bake ancestor-write swarm")) return 1;

    d8_swarm_set_tile_params_direct(swarm, 0, 1, 1000, 0, 0, 1);
    d8_swarm_set_tile_params_direct(swarm, 1, 1, 1000, 0, 0, 1);
    d8_swarm_set_tile_pattern_id_direct(swarm, 0, 10);
    d8_swarm_set_tile_routing_masks(swarm, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1);
    d8_swarm_set_tile_routing_masks(swarm, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0);
    set_all_weights(swarm, 0, 7);
    set_all_weights(swarm, 1, 7);

    uint8_t ingress[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    d8_flash_result_t first = d8_swarm_ev_flash(swarm, 20, ingress);
    d8_flash_result_t result = d8_swarm_ev_flash(swarm, 21, ingress);

    int bus_nonzero = 0;
    for (size_t i = 0; i < 8; i++) {
        if (result.readout16[i] != 0) bus_nonzero = 1;
    }

    int failed = 0;
    failed |= expect(first.st.code == D8_STATUS_OK, "parent-lock flash must succeed");
    failed |= expect(result.st.code == D8_STATUS_OK, "ancestor-write flash must succeed");
    failed |= expect(d8_swarm_get_tile_locked(swarm, 0), "parent tile must lock");
    failed |= expect(bus_nonzero, "BUS_W child must write when parent is locked ancestor");

    d8_swarm_destroy(swarm);
    return failed ? 1 : 0;
}

int main(void) {
    printf("========================================\n");
    printf("DECIMA-8 Flash Test\n");
    printf("========================================\n\n");
    
    /* Generate test bake */
    uint8_t bake_data[240000];
    size_t bake_size = 0;
    
    printf("Generating test bake...\n");
    if (d8_bake_gen_test(bake_data, &bake_size) != 0) {
        fprintf(stderr, "Failed to generate bake\n");
        return 1;
    }
    printf("Generated %zu bytes\n\n", bake_size);
    
    /* Create swarm */
    printf("Creating swarm...\n");
    d8_swarm_t* swarm = d8_swarm_create();
    if (!swarm) {
        fprintf(stderr, "Failed to create swarm\n");
        return 1;
    }
    
    /* Apply bake */
    printf("Applying bake...\n");
    d8_status_t st = d8_swarm_ev_bake(swarm, bake_data, bake_size);
    if (st.code != D8_STATUS_OK) {
        fprintf(stderr, "Failed to apply bake: %d - %s\n", st.code, st.msg);
        d8_swarm_destroy(swarm);
        return 1;
    }
    printf("Bake applied successfully\n\n");
    
    /* Test flash with different VSB patterns */
    uint8_t vsb_patterns[3][8] = {
        {0, 0, 0, 0, 0, 0, 0, 0},  /* All zeros */
        {15, 15, 15, 15, 15, 15, 15, 15},  /* All max */
        {5, 10, 15, 10, 5, 0, 5, 10}  /* Mixed */
    };
    
    printf("Running flash tests...\n");
    for (int i = 0; i < 3; i++) {
        printf("  Pattern %d: [%d,%d,%d,%d,%d,%d,%d,%d] ... ", 
               i + 1,
               vsb_patterns[i][0], vsb_patterns[i][1], vsb_patterns[i][2], vsb_patterns[i][3],
               vsb_patterns[i][4], vsb_patterns[i][5], vsb_patterns[i][6], vsb_patterns[i][7]);
        
        d8_flash_result_t result = d8_swarm_ev_flash(swarm, (uint32_t)i, vsb_patterns[i]);
        
        if (result.st.code != D8_STATUS_OK) {
            printf("FAILED: %s\n", result.st.msg);
        } else {
            printf("OK (cycle_time=%d μs)\n", (int)result.st.code);
        }
    }

    if (test_strict_ingress()) {
        d8_swarm_destroy(swarm);
        return 1;
    }
    if (test_winner_collide_autoreset()) {
        d8_swarm_destroy(swarm);
        return 1;
    }
    if (test_locked_ancestor_write()) {
        d8_swarm_destroy(swarm);
        return 1;
    }
    
    /* Cleanup */
    d8_swarm_destroy(swarm);
    
    printf("\n========================================\n");
    printf("All tests passed!\n");
    printf("========================================\n");
    
    return 0;
}
