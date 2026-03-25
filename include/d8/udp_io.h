/*
 * DECIMA-8 Core - Pure C Implementation
 * UDP Socket I/O for network telemetry
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"
#include "d8/udp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define D8_UDP_MAX_HOST_LEN 256
#define D8_UDP_RECV_TIMEOUT_MS 200
#define D8_UDP_MAX_PACKETS 10000

/* ============================================================================
 * UDP Sender
 * ============================================================================ */

typedef struct {
    d8_u8  host[D8_UDP_MAX_HOST_LEN];
    d8_u16 port;
    d8_u64 sent_count;
    d8_socket_t socket;
    int initialized;
} d8_udp_sender_t;

/**
 * @brief Initialize UDP sender
 * @param sender Pointer to sender structure
 * @return 0 on success, -1 on error
 */
int d8_udp_sender_init(d8_udp_sender_t* sender);

/**
 * @brief Cleanup UDP sender resources
 * @param sender Pointer to sender structure
 */
void d8_udp_sender_cleanup(d8_udp_sender_t* sender);

/**
 * @brief Open UDP sender socket
 * @param sender Pointer to sender structure
 * @param host Destination host (IP address or hostname)
 * @param port Destination port
 * @return 0 on success, -1 on error
 */
int d8_udp_sender_open(d8_udp_sender_t* sender, const char* host, d8_u16 port);

/**
 * @brief Close UDP sender socket
 * @param sender Pointer to sender structure
 */
void d8_udp_sender_close(d8_udp_sender_t* sender);

/**
 * @brief Send UDP packet
 * @param sender Pointer to sender structure
 * @param packet Pointer to packet to send
 * @return 0 on success, -1 on error
 */
int d8_udp_sender_send(d8_udp_sender_t* sender, const d8_udp_packet_t* packet);

/**
 * @brief Send raw UDP data
 * @param sender Pointer to sender structure
 * @param data Pointer to data buffer
 * @param size Size of data in bytes
 * @return 0 on success, -1 on error
 */
int d8_udp_sender_send_raw(d8_udp_sender_t* sender, const d8_u8* data, d8_size_t size);

/* ============================================================================
 * UDP Receiver
 * ============================================================================ */

/**
 * @brief Callback type for received packets
 * @param packet Pointer to received packet
 * @param user_data User-defined data passed to callback
 */
typedef void (*d8_udp_packet_handler_t)(const d8_udp_packet_t* packet, void* user_data);

typedef struct {
    d8_u16 port;
    d8_u64 recv_count;
    d8_socket_t socket;
    int running;
    int initialized;
    d8_udp_packet_handler_t handler;
    void* user_data;
    d8_thread_t thread;
} d8_udp_receiver_t;

/**
 * @brief Initialize UDP receiver
 * @param receiver Pointer to receiver structure
 * @return 0 on success, -1 on error
 */
int d8_udp_receiver_init(d8_udp_receiver_t* receiver);

/**
 * @brief Cleanup UDP receiver resources
 * @param receiver Pointer to receiver structure
 */
void d8_udp_receiver_cleanup(d8_udp_receiver_t* receiver);

/**
 * @brief Start UDP receiver thread
 * @param receiver Pointer to receiver structure
 * @param port Port to listen on
 * @param handler Callback function for received packets
 * @param user_data User-defined data passed to callback
 * @return 0 on success, -1 on error
 */
int d8_udp_receiver_start(d8_udp_receiver_t* receiver, d8_u16 port,
                           d8_udp_packet_handler_t handler, void* user_data);

/**
 * @brief Stop UDP receiver thread
 * @param receiver Pointer to receiver structure
 */
void d8_udp_receiver_stop(d8_udp_receiver_t* receiver);

/**
 * @brief Check if receiver is running
 * @param receiver Pointer to receiver structure
 * @return non-zero if running, 0 otherwise
 */
int d8_udp_receiver_is_running(const d8_udp_receiver_t* receiver);

#ifdef __cplusplus
}
#endif
