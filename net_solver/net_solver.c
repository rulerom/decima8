/*
 * DECIMA-8 Network Solver
 *
 * Autonomous UDP machine for processing VSB streams
 * Receives VSB frames and commands via UDP, sends solutions back
 *
 * All rights belong to the ORDEN (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#include "d8/swarm.h"
#include "d8/bake.h"
#include "d8/udp_io.h"
#include "d8/udp_protocol.h"

/* ============================================================================
 * Global state
 * ============================================================================ */

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

typedef struct {
    /* Network */
    d8_u16 recv_port;       /* Port to receive VSB/commands (default: 9901) */
    d8_u16 send_port;       /* Port to send solutions (default: 9902) */
    char send_host[256];    /* Destination host for solutions (default: 127.0.0.1) */
    
    /* Bake */
    char bake_path[512];    /* Path to D8P bake file */
    
    /* Options */
    int reset_on_solution;  /* Reset swarm after each solution */
    int verbose;            /* Verbose output (1=solutions, 2=full trace) */
} net_solver_config_t;

/* ============================================================================
 * Solver context
 * ============================================================================ */

typedef struct {
    /* Core */
    d8_swarm_t* swarm;
    uint8_t* bake_data;
    size_t bake_size;
    size_t active_tile_count;  /* Number of active tiles */
    
    /* Network */
    d8_udp_receiver_t receiver;
    d8_udp_sender_t sender;
    
    /* Config */
    net_solver_config_t config;
    
    /* Statistics */
    d8_u64 frames_processed;
    d8_u64 solutions_sent;
    d8_u64 resets_performed;
} net_solver_t;

/* ============================================================================
 * VSB decoder
 * ============================================================================ */

/* 
 * IDE sends VSB as 8 bytes directly in bus16 field (each byte 0..15)
 * No decoding needed - just copy from packet
 */

/* ============================================================================
 * Print activation log (verbose mode, level 2)
 * ============================================================================ */

static void print_activation_log(net_solver_t* solver, d8_u32 frame_tag, const d8_u8* vsb) {
    printf("[Frame %u] VSB IN: %u|%u|%u|%u|%u|%u|%u|%u\n",
           frame_tag, vsb[0], vsb[1], vsb[2], vsb[3], vsb[4], vsb[5], vsb[6], vsb[7]);
    
    /* Print thr_cur for tiles with pattern_id != 0 */
    printf("  Active tiles (thr_cur != 0):\n");
    int count = 0;
    for (size_t t = 0; t < solver->active_tile_count && count < 20; t++) {
        d8_i16 thr = d8_swarm_get_tile_thr_cur(solver->swarm, t);
        d8_u16 pattern = d8_swarm_get_tile_pattern_id(solver->swarm, t);
        if (pattern != 0 && thr != 0) {
            printf("    t%zu: thr=%5d pattern=%u\n", t, thr, pattern);
            count++;
        }
    }
    if (count >= 20) {
        printf("    ... (more tiles)\n");
    }
}

/* ============================================================================
 * UDP packet handler (receiver callback)
 * ============================================================================ */

typedef struct {
    net_solver_t* solver;
} packet_handler_context_t;

static void on_udp_packet_received(const d8_udp_packet_t* packet, void* user_data) {
    packet_handler_context_t* ctx = (packet_handler_context_t*)user_data;
    net_solver_t* solver = ctx->solver;
    
    if (!solver || !solver->swarm) return;
    
    /* Verbose level 2: log input and activation */
    if (solver->config.verbose >= 2) {
        print_activation_log(solver, packet->frame_tag, packet->bus16);
    }
    
    /* Execute flash with bus16 directly as VSB ingress */
    d8_flash_result_t result = d8_swarm_ev_flash(solver->swarm, packet->frame_tag, packet->bus16);
    
    if (result.st.code != D8_STATUS_OK) {
        if (solver->config.verbose >= 2) {
            fprintf(stderr, "Flash failed (tag=%u): %s\n", packet->frame_tag, result.st.msg);
        }
        return;
    }
    
    solver->frames_processed++;
    
    /* Get snapshot */
    const d8_view_snapshot_t* snapshot = d8_swarm_get_snapshot(solver->swarm);
    if (!snapshot) return;
    
    /* Check for solutions and send via UDP */
    int has_solution = 0;
    for (d8_u8 domain = 0; domain < D8_K_DOMAINS; domain++) {
        d8_u16 tile_id = snapshot->winner_tile_id[domain];
        d8_u16 pattern_id = snapshot->winner_pattern_id[domain];
        
        /* Skip if no winner */
        if (tile_id == 0xFFFF) continue;
        
        /* Prepare solution packet */
        d8_udp_packet_t out_packet;
        d8_udp_packet_init(&out_packet);
        
        out_packet.frame_tag = packet->frame_tag;
        out_packet.domain_id = domain;
        out_packet.pattern_id = pattern_id;
        out_packet.winner_tile_id = tile_id;
        out_packet.collision_mask16 = (snapshot->collide_mask16 & (1 << domain)) ? 1 : 0;
        
        /* Copy bus16 from snapshot (NOT from input!) */
        for (int i = 0; i < 8; i++) {
            out_packet.bus16[i] = snapshot->bus16[i];
        }
        
        /* Set flags */
        d8_udp_packet_set_flags(&out_packet, 1, 1, 1, 1);
        out_packet.flags32_last = snapshot->flags32_last;
        out_packet.cycle_time_us = snapshot->cycle_time_us;
        
        /* Send solution */
        if (d8_udp_sender_send(&solver->sender, &out_packet) == 0) {
            solver->solutions_sent++;
            has_solution = 1;
            
            /* Verbose level 1: log solutions */
            if (solver->config.verbose >= 1) {
                printf("SOL tag=%u dom=%u tile=%u pat=%u bus=%u|%u|%u|%u|%u|%u|%u|%u\n",
                       packet->frame_tag, domain, tile_id, pattern_id,
                       snapshot->bus16[0], snapshot->bus16[1], snapshot->bus16[2], snapshot->bus16[3],
                       snapshot->bus16[4], snapshot->bus16[5], snapshot->bus16[6], snapshot->bus16[7]);
            }
        }
    }
    
    /* Reset if configured and solution found */
    if (solver->config.reset_on_solution && has_solution) {
        d8_swarm_full_reset(solver->swarm);
        d8_swarm_ev_bake(solver->swarm, solver->bake_data, solver->bake_size);
        solver->resets_performed++;
        
        if (solver->config.verbose >= 2) {
            printf("  RESET: swarm reset and bake reapplied\n");
        }
    }
}

/* ============================================================================
 * Net Solver lifecycle
 * ============================================================================ */

static int net_solver_init(net_solver_t* solver, const net_solver_config_t* config) {
    memset(solver, 0, sizeof(net_solver_t));
    memcpy(&solver->config, config, sizeof(net_solver_config_t));
    
    /* Load bake */
    if (config->bake_path[0] != '\0') {
        FILE* f = fopen(config->bake_path, "rb");
        if (!f) {
            fprintf(stderr, "Failed to open bake file: %s\n", config->bake_path);
            return -1;
        }
        
        fseek(f, 0, SEEK_END);
        solver->bake_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        solver->bake_data = (uint8_t*)malloc(solver->bake_size);
        if (!solver->bake_data) {
            fprintf(stderr, "Failed to allocate bake buffer\n");
            fclose(f);
            return -1;
        }
        
        fread(solver->bake_data, 1, solver->bake_size, f);
        fclose(f);
        
        printf("Loaded %zu bytes from %s\n", solver->bake_size, config->bake_path);
    }
    
    /* Create swarm */
    solver->swarm = d8_swarm_create();
    if (!solver->swarm) {
        fprintf(stderr, "Failed to create swarm\n");
        return -1;
    }
    
    /* Apply bake */
    if (solver->bake_data) {
        d8_status_t st = d8_swarm_ev_bake(solver->swarm, solver->bake_data, solver->bake_size);
        if (st.code != D8_STATUS_OK) {
            fprintf(stderr, "Failed to apply bake: %d - %s\n", st.code, st.msg);
            return -1;
        }
        printf("Bake applied successfully\n");
        
        /* Store active tile count */
        solver->active_tile_count = d8_swarm_get_active_tile_count(solver->swarm);
        printf("Active tiles: %zu\n", solver->active_tile_count);
    }
    
    /* Initialize UDP receiver */
    if (d8_udp_receiver_init(&solver->receiver) != 0) {
        fprintf(stderr, "Failed to initialize UDP receiver\n");
        return -1;
    }
    
    /* Initialize UDP sender */
    if (d8_udp_sender_init(&solver->sender) != 0) {
        fprintf(stderr, "Failed to initialize UDP sender\n");
        return -1;
    }
    
    /* Open UDP sender */
    if (d8_udp_sender_open(&solver->sender, config->send_host, config->send_port) != 0) {
        fprintf(stderr, "Failed to open UDP sender to %s:%u\n", config->send_host, config->send_port);
        return -1;
    }
    
    printf("UDP sender opened: %s:%u\n", config->send_host, config->send_port);
    
    return 0;
}

static void net_solver_start(net_solver_t* solver) {
    packet_handler_context_t* ctx = (packet_handler_context_t*)malloc(sizeof(packet_handler_context_t));
    ctx->solver = solver;
    
    /* Start UDP receiver */
    if (d8_udp_receiver_start(&solver->receiver, solver->config.recv_port,
                               on_udp_packet_received, ctx) == 0) {
        printf("UDP receiver started on port %u\n", solver->config.recv_port);
    } else {
        fprintf(stderr, "Failed to start UDP receiver on port %u\n", solver->config.recv_port);
    }
}

static void net_solver_stop(net_solver_t* solver) {
    d8_udp_receiver_stop(&solver->receiver);
    d8_udp_sender_close(&solver->sender);
}

static void net_solver_cleanup(net_solver_t* solver) {
    if (solver->swarm) {
        d8_swarm_destroy(solver->swarm);
    }
    if (solver->bake_data) {
        free(solver->bake_data);
    }
    d8_udp_sender_cleanup(&solver->sender);
    d8_udp_receiver_cleanup(&solver->receiver);
}

static void net_solver_print_stats(net_solver_t* solver) {
    printf("\n========================================\n");
    printf("Net Solver Statistics\n");
    printf("========================================\n");
    printf("Frames processed: %llu\n", (unsigned long long)solver->frames_processed);
    printf("Solutions sent:   %llu\n", (unsigned long long)solver->solutions_sent);
    printf("Resets performed: %llu\n", (unsigned long long)solver->resets_performed);
    printf("========================================\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <bake_file>\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -r <port>   Receive port (default: 9901)\n");
    printf("  -s <port>   Send port (default: 9902)\n");
    printf("  -h <host>   Send host (default: 127.0.0.1)\n");
    printf("  -R          Reset swarm after each solution\n");
    printf("  -v          Verbose: show solutions sent\n");
    printf("  -vv         Very verbose: show input VSB and tile activation\n");
    printf("  --help      Show this help\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s -r 9901 -s 9902 -h 192.168.1.100 -R -v bake.d8p\n", prog);
    printf("\n");
    printf("Network protocol:\n");
    printf("  Input:  UDP packets with VSB in bus16 field (8 bytes, 0..15 each)\n");
    printf("  Output: UDP packets with solutions (tile_id, pattern_id, domain, bus16)\n");
}

int main(int argc, char** argv) {
    net_solver_config_t config = {0};
    
    /* Defaults */
    config.recv_port = 9901;
    config.send_port = 9902;
    strncpy(config.send_host, "127.0.0.1", sizeof(config.send_host) - 1);
    config.reset_on_solution = 0;
    config.verbose = 0;
    
    /* Parse arguments */
    int bake_arg = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            config.recv_port = (d8_u16)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config.send_port = (d8_u16)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            strncpy(config.send_host, argv[++i], sizeof(config.send_host) - 1);
        } else if (strcmp(argv[i], "-R") == 0) {
            config.reset_on_solution = 1;
        } else if (strcmp(argv[i], "-vv") == 0) {
            config.verbose = 2;
        } else if (strcmp(argv[i], "-v") == 0) {
            config.verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            bake_arg = i;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (bake_arg < 0) {
        fprintf(stderr, "Error: Bake file is required\n");
        print_usage(argv[0]);
        return 1;
    }
    
    strncpy(config.bake_path, argv[bake_arg], sizeof(config.bake_path) - 1);
    
    /* Setup signal handler */
    signal(SIGINT, signal_handler);
#ifdef SIGTERM
    signal(SIGTERM, signal_handler);
#endif
    
    printf("========================================\n");
    printf("Decima-8 Network Solver\n");
    printf("========================================\n\n");
    
    printf("Configuration:\n");
    printf("  Receive port: %u\n", config.recv_port);
    printf("  Send port:    %u\n", config.send_port);
    printf("  Send host:    %s\n", config.send_host);
    printf("  Reset on solution: %s\n", config.reset_on_solution ? "yes" : "no");
    printf("  Verbose:      %d (%s)\n", config.verbose,
           config.verbose == 0 ? "off" :
           config.verbose == 1 ? "solutions" : "full trace");
    printf("  Bake file:    %s\n", config.bake_path);
    printf("\n");
    
    /* Initialize solver */
    net_solver_t solver;
    if (net_solver_init(&solver, &config) != 0) {
        fprintf(stderr, "Failed to initialize net solver\n");
        return 1;
    }
    
    /* Start solver */
    net_solver_start(&solver);
    
    printf("\nPress Ctrl+C to stop...\n\n");
    
    /* Main loop */
    while (g_running) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    
    /* Stop solver */
    printf("\nStopping...\n");
    net_solver_stop(&solver);
    
    /* Print statistics */
    net_solver_print_stats(&solver);
    
    /* Cleanup */
    net_solver_cleanup(&solver);
    
    printf("Net solver stopped.\n");
    return 0;
}
