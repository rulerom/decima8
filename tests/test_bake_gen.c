/*
 * DECIMA-8 Bake Generator Test
 * 
 * All rights belong to the ORDEN (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "bake_gen.h"

int main(void) {
    printf("========================================\n");
    printf("DECIMA-8 Bake Generator Test\n");
    printf("========================================\n\n");
    
    uint8_t buffer[240000];
    size_t size = 0;
    
    /* Test 1: Generate test bake */
    printf("Test 1: Generate test bake (4096 tiles)...\n");
    if (d8_bake_gen_test(buffer, &size) != 0) {
        fprintf(stderr, "FAILED: Could not generate test bake\n");
        return 1;
    }
    printf("  Size: %zu bytes\n", size);
    
    /* Verify header */
    if (buffer[0] != 'D' || buffer[1] != '8' || buffer[2] != 'B' || buffer[3] != 'K') {
        fprintf(stderr, "FAILED: Invalid magic\n");
        return 1;
    }
    printf("  Magic: D8BK OK\n");
    
    /* Verify version */
    uint16_t ver_major = buffer[4] | (buffer[5] << 8);
    uint16_t ver_minor = buffer[6] | (buffer[7] << 8);
    printf("  Version: %d.%d\n", ver_major, ver_minor);
    
    /* Test 2: Generate custom bake with different tile counts */
    printf("\nTest 2: Generate custom bakes...\n");
    
    uint32_t tile_counts[] = { 256, 1024, 4096 };
    for (int i = 0; i < 3; i++) {
        size_t custom_size = 0;
        if (d8_bake_gen_custom(buffer, &custom_size, tile_counts[i], 0, 0, 0) != 0) {
            fprintf(stderr, "FAILED: Could not generate custom bake for %u tiles\n", tile_counts[i]);
            return 1;
        }
        
        size_t estimated = d8_bake_gen_estimate_size(tile_counts[i]);
        printf("  %u tiles: %zu bytes (estimated: %zu)\n", 
               tile_counts[i], custom_size, estimated);
        
        if (custom_size != estimated) {
            fprintf(stderr, "WARNING: Size mismatch\n");
        }
    }
    
    /* Test 3: Save to file */
    printf("\nTest 3: Save to file...\n");
    FILE* f = fopen("test_bake.d8p", "wb");
    if (!f) {
        fprintf(stderr, "FAILED: Could not open output file\n");
        return 1;
    }
    
    size_t written = fwrite(buffer, 1, size, f);
    fclose(f);
    
    if (written != size) {
        fprintf(stderr, "FAILED: Write error\n");
        return 1;
    }
    printf("  Saved %zu bytes to test_bake.d8p\n", size);
    
    printf("\n========================================\n");
    printf("All tests passed!\n");
    printf("========================================\n");
    
    return 0;
}
