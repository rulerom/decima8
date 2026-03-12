/*
 * DECIMA-8 Create Bake Example
 * 
 * Shows how to generate a custom bake file
 * 
 * All rights belong to the ORDEN (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "bake_gen.h"

int main(int argc, char** argv) {
    const char* output_file = "custom_bake.d8p";
    uint32_t tile_count = 256;
    int16_t thr_lo = 0;
    int16_t thr_hi = 0;
    uint16_t decay16 = 0;
    
    /* Parse arguments (optional) */
    if (argc > 1) output_file = argv[1];
    if (argc > 2) tile_count = (uint32_t)atoi(argv[2]);
    if (argc > 3) thr_lo = (int16_t)atoi(argv[3]);
    if (argc > 4) thr_hi = (int16_t)atoi(argv[4]);
    if (argc > 5) decay16 = (uint16_t)atoi(argv[5]);
    
    printf("DECIMA-8 Create Bake Example\n");
    printf("=============================\n\n");
    
    printf("Parameters:\n");
    printf("  Output file: %s\n", output_file);
    printf("  Tile count:  %u\n", tile_count);
    printf("  thr_lo:      %d\n", thr_lo);
    printf("  thr_hi:      %d\n", thr_hi);
    printf("  decay16:     %u\n\n", decay16);
    
    /* Allocate buffer */
    size_t estimated_size = d8_bake_gen_estimate_size(tile_count);
    uint8_t* buffer = (uint8_t*)malloc(estimated_size);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return 1;
    }
    
    /* Generate bake */
    printf("Generating bake...\n");
    size_t bake_size = 0;
    int ret = d8_bake_gen_custom(buffer, &bake_size, tile_count, thr_lo, thr_hi, decay16);
    if (ret != 0) {
        fprintf(stderr, "Failed to generate bake\n");
        free(buffer);
        return 1;
    }
    printf("Generated %zu bytes\n\n", bake_size);
    
    /* Save to file */
    printf("Saving to %s...\n", output_file);
    FILE* f = fopen(output_file, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open output file\n");
        free(buffer);
        return 1;
    }
    
    size_t written = fwrite(buffer, 1, bake_size, f);
    fclose(f);
    
    if (written != bake_size) {
        fprintf(stderr, "Write error\n");
        free(buffer);
        return 1;
    }
    
    printf("Saved %zu bytes\n\n", written);
    
    /* Cleanup */
    free(buffer);
    
    printf("Bake created successfully!\n");
    printf("\nTo use this bake:\n");
    printf("  1. Load it in IDE: File -> Load Bake\n");
    printf("  2. Or use with d8_swarm_bench:\n");
    printf("     d8_swarm_bench tape.vsb %s\n", output_file);
    
    return 0;
}
