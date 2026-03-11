/*
 * DECIMA-8 Bake Generator Benchmark
 */

#include "bake_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>

static double get_time_ms(void) {
    static LARGE_INTEGER freq;
    static int freq_init = 0;
    LARGE_INTEGER count;
    
    if (!freq_init) {
        QueryPerformanceFrequency(&freq);
        freq_init = 1;
    }
    
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
}

#else
#include <sys/time.h>

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

#endif

#define NUM_ITERATIONS 100

int main(int argc, char** argv) {
    uint8_t* buffer = (uint8_t*)malloc(240000);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return 1;
    }
    
    printf("DECIMA-8 Bake Generator Benchmark\n");
    printf("==================================\n\n");
    
    /* Test different tile counts */
    uint32_t tile_counts[] = { 256, 1024, 4096 };
    size_t num_configs = sizeof(tile_counts) / sizeof(tile_counts[0]);
    
    for (size_t i = 0; i < num_configs; i++) {
        uint32_t tile_count = tile_counts[i];
        size_t estimated = d8_bake_gen_estimate_size(tile_count);
        
        printf("Tile count: %u (estimated size: %zu bytes)\n", tile_count, estimated);
        
        double total_time = 0.0;
        size_t actual_size = 0;
        
        for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
            double start = get_time_ms();
            
            size_t size = 0;
            int ret = d8_bake_gen_custom(buffer, &size, tile_count, 0, 0, 0);
            
            double end = get_time_ms();
            
            if (ret != 0) {
                fprintf(stderr, "Generation failed at iteration %d\n", iter);
                free(buffer);
                return 1;
            }
            
            total_time += (end - start);
            actual_size = size;
        }
        
        double avg_time = total_time / NUM_ITERATIONS;
        double throughput = (double)actual_size / (avg_time / 1000.0) / (1024.0 * 1024.0);
        
        printf("  Actual size: %zu bytes\n", actual_size);
        printf("  Avg time: %.3f ms\n", avg_time);
        printf("  Throughput: %.2f MB/s\n", throughput);
        printf("\n");
    }
    
    free(buffer);
    
    printf("Benchmark complete.\n");
    return 0;
}
