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
    
    /* Cleanup */
    d8_swarm_destroy(swarm);
    
    printf("\n========================================\n");
    printf("All tests passed!\n");
    printf("========================================\n");
    
    return 0;
}
