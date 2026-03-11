/*
 * DECIMA-8 Bake Generator Test
 */

#include "bake_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    uint8_t* buffer = (uint8_t*)malloc(240000);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return 1;
    }
    
    size_t size = 0;
    int ret = d8_bake_gen_test(buffer, &size);
    
    if (ret != 0) {
        fprintf(stderr, "Failed to generate bake: %d\n", ret);
        free(buffer);
        return 1;
    }
    
    printf("Generated bake blob: %zu bytes\n", size);
    
    /* Verify header */
    if (buffer[0] != 'D' || buffer[1] != '8' || buffer[2] != 'B' || buffer[3] != 'K') {
        fprintf(stderr, "Invalid magic\n");
        free(buffer);
        return 1;
    }
    
    printf("Magic: D8BK OK\n");
    printf("Version: %d.%d\n", buffer[4], buffer[5]);
    
    /* Save to file */
    FILE* f = fopen("test_bake.d8p", "wb");
    if (!f) {
        fprintf(stderr, "Failed to open output file\n");
        free(buffer);
        return 1;
    }
    
    fwrite(buffer, 1, size, f);
    fclose(f);
    
    printf("Saved to test_bake.d8p\n");
    free(buffer);
    
    return 0;
}
