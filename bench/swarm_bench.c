/*
 * DECIMA-8 Swarm Benchmark
 *
 * Прогон VSB ленты через D8P bake с замером времени цикла
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "d8/swarm.h"
#include "d8/bake.h"
#include "d8/vsb.h"

/* ============================================================================
 * Timing
 * ============================================================================ */

#ifdef _WIN32
static LARGE_INTEGER g_freq;
static int g_freq_init = 0;

static double get_time_us(void) {
    LARGE_INTEGER count;
    if (!g_freq_init) {
        QueryPerformanceFrequency(&g_freq);
        g_freq_init = 1;
    }
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000000.0 / (double)g_freq.QuadPart;
}
#else
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}
#endif

/* ============================================================================
 * Bake Loader
 * ============================================================================ */

static int load_bake_file(const char* path, uint8_t** bake_data, size_t* bake_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open D8P file: %s\n", path);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    *bake_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    *bake_data = (uint8_t*)malloc(*bake_size);
    if (!*bake_data) {
        fprintf(stderr, "Failed to allocate bake buffer\n");
        fclose(f);
        return -1;
    }
    
    fread(*bake_data, 1, *bake_size, f);
    fclose(f);
    
    printf("Loaded %zu bytes from %s\n", *bake_size, path);
    return 0;
}

/* ============================================================================
 * Benchmark
 * ============================================================================ */

typedef struct {
    double min_us;
    double max_us;
    double total_us;
    size_t count;
    size_t solutions;      /* Number of solutions found */
    size_t collisions;     /* Number of collisions */
    int reset_on_solution; /* Reset swarm after each solution */
} benchmark_stats_t;

static void run_benchmark(d8_swarm_t* swarm, const d8_vsb_tape_t* tape, benchmark_stats_t* stats,
                          const uint8_t* bake_data, size_t bake_size) {
    stats->min_us = 1e9;
    stats->max_us = 0;
    stats->total_us = 0;
    stats->count = 0;
    stats->solutions = 0;
    stats->collisions = 0;

    printf("\nRunning benchmark (%zu frames)...\n", tape->count);

    for (size_t i = 0; i < tape->count; i++) {
        d8_u32 frame_tag = tape->frames[i].frame_tag;
        const d8_u8* vsb_data = tape->frames[i].data;

        double start = get_time_us();

        d8_flash_result_t result = d8_swarm_ev_flash(swarm, frame_tag, vsb_data);

        double end = get_time_us();
        double cycle_time = end - start;

        if (result.st.code != D8_STATUS_OK) {
            fprintf(stderr, "Flash failed at frame %zu (tag=%u): %s\n", i, frame_tag, result.st.msg);
            continue;
        }

        /* Count solutions and collisions */
        const d8_view_snapshot_t* snapshot = d8_swarm_get_snapshot(swarm);
        int has_solution = 0;
        if (snapshot) {
            for (d8_u8 d = 0; d < D8_K_DOMAINS; d++) {
                if (snapshot->winner_tile_id[d] != 0xFFFF) {
                    stats->solutions++;
                    has_solution = 1;
                }
                if (snapshot->collide_mask16 & (1 << d)) {
                    stats->collisions++;
                }
            }
        }

        /* Reset swarm if option is set and solution found */
        if (stats->reset_on_solution && has_solution) {
            d8_swarm_full_reset(swarm);
        }

        if (cycle_time < stats->min_us) stats->min_us = cycle_time;
        if (cycle_time > stats->max_us) stats->max_us = cycle_time;
        stats->total_us += cycle_time;
        stats->count++;

        /* Progress indicator */
        if ((i + 1) % 100 == 0) {
            printf("  Progress: %zu/%zu frames (%.1f%%)\n",
                   i + 1, tape->count, (double)(i + 1) * 100.0 / (double)tape->count);
        }
    }
}

static void print_stats(benchmark_stats_t* stats, size_t active_tile_count) {
    if (stats->count == 0) {
        printf("No frames processed\n");
        return;
    }

    double avg_us = stats->total_us / (double)stats->count;
    double fps = 1000000.0 / avg_us;
    double avg_per_solution = stats->solutions > 0 ? stats->total_us / (double)stats->solutions : 0;

    printf("\n");
    printf("========================================\n");
    printf("Benchmark Results\n");
    printf("========================================\n");
    printf("Swarm size:       %zu tiles\n", active_tile_count);
    printf("Frames (cycles):  %zu\n", stats->count);
    printf("Solutions found:  %zu\n", stats->solutions);
    printf("Collisions:       %zu\n", stats->collisions);
    if (stats->reset_on_solution) {
        printf("Resets:           %zu\n", stats->solutions);
    }
    printf("Cycle Time:\n");
    printf("  Min:  %.1f us (%.1f FPS)\n", stats->min_us, 1000000.0 / stats->min_us);
    printf("  Max:  %.1f us (%.1f FPS)\n", stats->max_us, 1000000.0 / stats->max_us);
    printf("  Avg:  %.1f us (%.1f FPS)\n", avg_us, fps);
    printf("Total time:       %.2f ms\n", stats->total_us / 1000.0);
    if (stats->solutions > 0) {
        printf("Avg per solution: %.1f us\n", avg_per_solution);
    }
    printf("========================================\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <vsb_file> <d8p_file>\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -R          Reset swarm after each solution\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  vsb_file  - VSB tape file (e.g., tape.vsb)\n");
    printf("  d8p_file  - D8P bake file (e.g., bake.d8p)\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s tape.vsb bake.d8p\n", prog);
    printf("  %s -R tape.vsb bake.d8p\n", prog);
}

int main(int argc, char** argv) {
    /* Parse options */
    int reset_on_solution = 0;
    int arg_start = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-R") == 0) {
            reset_on_solution = 1;
            arg_start = i + 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            break;
        }
    }

    int remaining_args = argc - arg_start;
    if (remaining_args != 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* vsb_path = argv[arg_start];
    const char* d8p_path = argv[arg_start + 1];

    printf("========================================\n");
    printf("Decima-8 Swarm Benchmark\n");
    printf("========================================\n\n");
    printf("Options:\n");
    printf("  Reset on solution: %s\n", reset_on_solution ? "yes" : "no");
    printf("\n");
    
    /* Load VSB tape */
    d8_vsb_tape_t tape;
    if (d8_vsb_load(vsb_path, &tape) != 0) {
        return 1;
    }
    
    /* Load D8P bake */
    uint8_t* bake_data = NULL;
    size_t bake_size = 0;
    if (load_bake_file(d8p_path, &bake_data, &bake_size) != 0) {
        return 1;
    }
    
    /* Create swarm */
    printf("\nInitializing swarm...\n");
    d8_swarm_t* swarm = d8_swarm_create();
    if (!swarm) {
        fprintf(stderr, "Failed to create swarm\n");
        free(bake_data);
        return 1;
    }
    
    /* Apply bake */
    printf("Applying bake...\n");
    d8_status_t st = d8_swarm_ev_bake(swarm, bake_data, bake_size);
    if (st.code != D8_STATUS_OK) {
        fprintf(stderr, "Failed to apply bake: %d - %s\n", st.code, st.msg);
        d8_swarm_destroy(swarm);
        free(bake_data);
        return 1;
    }
    printf("Bake applied successfully\n");
    
    /* Run benchmark */
    benchmark_stats_t stats;
    stats.reset_on_solution = reset_on_solution;
    run_benchmark(swarm, &tape, &stats, bake_data, bake_size);

    /* Print results */
    print_stats(&stats, d8_swarm_get_active_tile_count(swarm));
    
    /* Cleanup */
    d8_swarm_destroy(swarm);
    free(bake_data);
    
    printf("\nBenchmark complete.\n");
    return 0;
}
