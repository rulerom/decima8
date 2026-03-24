/*
 * DECIMA-8 Swarm Solver
 *
 * Прогон VSB ленты через D8P bake с выводом решений
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
 * Options
 * ============================================================================ */

typedef struct {
    int console_only;      /* Вывод только в консоль */
    int reset_on_solution; /* Сброс swarm при получении решения */
    int skip_empty;        /* Не выводить пустые решения */
} solver_options_t;

static solver_options_t g_opts = {0, 0, 1}; /* skip_empty по умолчанию включён */

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
 * Solver
 * ============================================================================ */

typedef struct {
    size_t total_solutions;
    size_t total_collisions;
    double total_time_us;
} solver_stats_t;

static void run_solver(d8_swarm_t* swarm, const d8_vsb_tape_t* tape, solver_stats_t* stats,
                       FILE* out_file, const uint8_t* bake_data, size_t bake_size) {
    stats->total_solutions = 0;
    stats->total_collisions = 0;
    stats->total_time_us = 0;

    printf("\nRunning solver (%zu frames)...\n", tape->count);

    /* Write CSV header if not console only */
    if (!g_opts.console_only) {
        fprintf(out_file, "frame_tag,domain_id,tile_id,collision,pattern_id,bus16\n");
    }

    for (size_t i = 0; i < tape->count; i++) {
        d8_u32 frame_tag = tape->frames[i].frame_tag;
        const d8_u8* vsb_data = tape->frames[i].data;

        double start = get_time_us();

        d8_flash_result_t result = d8_swarm_ev_flash(swarm, frame_tag, vsb_data);

        double end = get_time_us();
        double cycle_time = end - start;
        stats->total_time_us += cycle_time;

        if (result.st.code != D8_STATUS_OK) {
            fprintf(stderr, "Flash failed at frame %zu (tag=%u): %s\n", i, frame_tag, result.st.msg);
            continue;
        }

        /* Get snapshot for detailed output */
        const d8_view_snapshot_t* snapshot = d8_swarm_get_snapshot(swarm);

        /* Check if any domain has winner or collision */
        int any_solution = 0;
        for (d8_u8 domain = 0; domain < D8_K_DOMAINS; domain++) {
            if (snapshot->winner_tile_id[domain] != 0xFFFF ||
                (snapshot->collide_mask16 & (1 << domain))) {
                any_solution = 1;
                break;
            }
        }

        /* Skip empty frames if option is set */
        if (g_opts.skip_empty && !any_solution) {
            continue;
        }

        int frame_had_solution = 0;

        /* Process each domain - output winners with pattern_id != 0 */
        for (d8_u8 domain = 0; domain < D8_K_DOMAINS; domain++) {
            d8_u16 tile_id = snapshot->winner_tile_id[domain];
            d8_u16 pattern_id = snapshot->winner_pattern_id[domain];
            int collision = (snapshot->collide_mask16 & (1 << domain)) ? 1 : 0;

            /* Skip domains with no winner (tile_id=0xFFFF) and no collision */
            if (tile_id == 0xFFFF && !collision) {
                continue;
            }

            /* Get bus16 for this domain (8 lanes) - format as value|value|... */
            char bus_str[32];
            snprintf(bus_str, sizeof(bus_str), "%u|%u|%u|%u|%u|%u|%u|%u",
                     snapshot->bus16[0], snapshot->bus16[1], snapshot->bus16[2], snapshot->bus16[3],
                     snapshot->bus16[4], snapshot->bus16[5], snapshot->bus16[6], snapshot->bus16[7]);

            /* Format: frame_tag,domain_id,tile_id,collision,pattern_id,bus16 */
            /* Use empty fields for no winner (like IDE) */
            if (g_opts.console_only) {
                if (tile_id == 0xFFFF) {
                    printf("%u,%u,,%d,,%s\n", frame_tag, domain, collision, bus_str);
                } else {
                    printf("%u,%u,%u,%d,%u,%s\n", frame_tag, domain, tile_id, collision, pattern_id, bus_str);
                }
            } else {
                if (tile_id == 0xFFFF) {
                    fprintf(out_file, "%u,%u,,%d,,%s\n", frame_tag, domain, collision, bus_str);
                } else {
                    fprintf(out_file, "%u,%u,%u,%d,%u,%s\n", frame_tag, domain, tile_id, collision, pattern_id, bus_str);
                }
            }

            stats->total_solutions++;
            if (collision) {
                stats->total_collisions++;
            }
            frame_had_solution = 1;
        }

        /* Reset swarm if option is set and solution was found */
        if (g_opts.reset_on_solution && frame_had_solution) {
            d8_swarm_full_reset(swarm);
            d8_swarm_ev_bake(swarm, bake_data, bake_size);
        }

        /* Progress indicator */
        if ((i + 1) % 100 == 0) {
            printf("  Progress: %zu/%zu frames (%.1f%%)\n",
                   i + 1, tape->count, (double)(i + 1) * 100.0 / (double)tape->count);
        }
    }
}

static void print_stats(solver_stats_t* stats) {
    printf("\n");
    printf("========================================\n");
    printf("Solver Results\n");
    printf("========================================\n");
    printf("Total solutions: %zu\n", stats->total_solutions);
    printf("Total collisions: %zu\n", stats->total_collisions);
    printf("Total time: %.2f ms\n", stats->total_time_us / 1000.0);
    if (stats->total_solutions > 0) {
        printf("Avg time per frame: %.1f us\n",
               stats->total_time_us / (double)stats->total_solutions);
    }
    printf("========================================\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <vsb_file> <d8p_file> [output_file]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -c          Console output only (no file output)\n");
    printf("  -r          Reset swarm after each solution\n");
    printf("  -a          Output all frames (including empty/no-fire)\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  vsb_file    - VSB tape file (e.g., tape.vsb)\n");
    printf("  d8p_file    - D8P bake file (e.g., bake.d8p)\n");
    printf("  output_file - Optional output CSV file (default: solutions.csv)\n");
    printf("\n");
    printf("Output format:\n");
    printf("  frame_tag,domain_id,tile_id,collision,pattern_id,bus16\n");
    printf("  (bus16 format: val|val|val|val|val|val|val|val)\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s -c -r tape.vsb bake.d8p\n", prog);
    printf("  %s -a tape.vsb bake.d8p solutions.csv\n", prog);
}

int main(int argc, char** argv) {
    /* Parse options */
    int arg_start = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char* p = argv[i] + 1; *p; p++) {
                switch (*p) {
                    case 'c':
                        g_opts.console_only = 1;
                        break;
                    case 'r':
                        g_opts.reset_on_solution = 1;
                        break;
                    case 'a':
                        g_opts.skip_empty = 0;
                        break;
                    case 'h':
                    case '?':
                        print_usage(argv[0]);
                        return 0;
                    default:
                        fprintf(stderr, "Unknown option: -%c\n", *p);
                        print_usage(argv[0]);
                        return 1;
                }
            }
            arg_start = i + 1;
        } else {
            break;
        }
    }

    int remaining_args = argc - arg_start;
    if (remaining_args < 2 || remaining_args > 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char* vsb_path = argv[arg_start];
    const char* d8p_path = argv[arg_start + 1];
    const char* output_path = (remaining_args == 3) ? argv[arg_start + 2] : "solutions.csv";

    /* If console_only is set, ignore output file */
    if (g_opts.console_only) {
        output_path = NULL;
    }

    printf("========================================\n");
    printf("Decima-8 Swarm Solver\n");
    printf("========================================\n\n");
    printf("Options:\n");
    printf("  Console output only: %s\n", g_opts.console_only ? "yes" : "no");
    printf("  Reset on solution:   %s\n", g_opts.reset_on_solution ? "yes" : "no");
    printf("  Skip empty frames:   %s\n", g_opts.skip_empty ? "yes" : "no");
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

    /* Open output file if needed */
    FILE* out_file = stdout;
    if (!g_opts.console_only && output_path) {
        out_file = fopen(output_path, "w");
        if (!out_file) {
            fprintf(stderr, "Failed to open output file: %s\n", output_path);
            d8_swarm_destroy(swarm);
            free(bake_data);
            return 1;
        }
        printf("Output file: %s\n", output_path);
    } else if (g_opts.console_only) {
        printf("Output: console\n");
    }

    /* Run solver */
    solver_stats_t stats;
    run_solver(swarm, &tape, &stats, out_file, bake_data, bake_size);

    /* Close output file if not stdout */
    if (!g_opts.console_only && out_file != stdout) {
        fclose(out_file);
    }

    /* Print results */
    print_stats(&stats);

    /* Cleanup */
    d8_swarm_destroy(swarm);
    free(bake_data);

    printf("\nSolver complete.\n");
    if (!g_opts.console_only && output_path) {
        printf("Results written to %s\n", output_path);
    }
    return 0;
}
