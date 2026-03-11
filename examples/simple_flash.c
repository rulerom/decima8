/*
 * DECIMA-8 Simple Flash Example
 * 
 * Shows how to create a swarm, apply a bake, and run flash
 * 
 * All rights belong to the ORDEN (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "d8/swarm.h"
#include "bake_gen.h"

int main(void) {
    printf("DECIMA-8 Simple Flash Example\n");
    printf("==============================\n\n");
    
    /* Step 1: Generate test bake */
    uint8_t bake_data[240000];
    size_t bake_size = 0;
    
    printf("1. Generating test bake...\n");
    if (d8_bake_gen_test(bake_data, &bake_size) != 0) {
        fprintf(stderr, "Failed to generate bake\n");
        return 1;
    }
    printf("   Generated %zu bytes\n\n", bake_size);
    
    /* Step 2: Create swarm */
    printf("2. Creating swarm...\n");
    d8_swarm_t* swarm = d8_swarm_create();
    if (!swarm) {
        fprintf(stderr, "Failed to create swarm\n");
        return 1;
    }
    printf("   Swarm created\n\n");
    
    /* Step 3: Apply bake */
    printf("3. Applying bake...\n");
    d8_status_t st = d8_swarm_ev_bake(swarm, bake_data, bake_size);
    if (st.code != D8_STATUS_OK) {
        fprintf(stderr, "Failed to apply bake: %d - %s\n", st.code, st.msg);
        d8_swarm_destroy(swarm);
        return 1;
    }
    printf("   Bake applied successfully\n\n");
    
    /* Step 4: Run flash with different VSB patterns */
    printf("4. Running flash cycles...\n");
    
    uint8_t vsb_patterns[3][8] = {
        {0, 0, 0, 0, 0, 0, 0, 0},
        {15, 15, 15, 15, 15, 15, 15, 15},
        {5, 10, 15, 10, 5, 0, 5, 10}
    };
    
    for (int i = 0; i < 3; i++) {
        printf("   Flash %d: [%d,%d,%d,%d,%d,%d,%d,%d] ... ", 
               i + 1,
               vsb_patterns[i][0], vsb_patterns[i][1], vsb_patterns[i][2], vsb_patterns[i][3],
               vsb_patterns[i][4], vsb_patterns[i][5], vsb_patterns[i][6], vsb_patterns[i][7]);
        
        d8_flash_result_t result = d8_swarm_ev_flash(swarm, (uint32_t)i, vsb_patterns[i]);
        
        if (result.st.code == D8_STATUS_OK) {
            printf("OK\n");
        } else {
            printf("FAILED: %s\n", result.st.msg);
        }
    }
    
    /* Step 5: Cleanup */
    printf("\n5. Cleanup...\n");
    d8_swarm_destroy(swarm);
    printf("   Done\n\n");
    
    printf("Example complete!\n");
    return 0;
}
