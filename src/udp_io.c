/*
 * DECIMA-8 Core - Pure C Implementation
 * UDP Socket I/O for network telemetry
 *
 * All rights belong to the ORDEN (c) 2026
 */

/* ============================================================================
 * Platform-specific includes (MUST be first for Windows)
 * ============================================================================ */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

typedef SOCKET socket_native_t;
#define INVALID_SOCKET_NATIVE INVALID_SOCKET
#define SOCKET_ERROR_NATIVE SOCKET_ERROR

#else /* POSIX */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

typedef int socket_native_t;
#define INVALID_SOCKET_NATIVE (-1)
#define SOCKET_ERROR_NATIVE (-1)

#endif

#include "d8/udp_io.h"
#include "d8/types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Helper macros for thread operations
 * ============================================================================ */

#ifdef _WIN32
#include <process.h>

static inline int d8_thread_create(d8_thread_t* thread, void (*start_routine)(void*), void* arg) {
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, 
        (unsigned int (__stdcall*)(void*))start_routine, arg, 0, NULL);
    if (h == NULL) return -1;
    *(HANDLE*)thread = h;
    return 0;
}

static inline int d8_thread_join(d8_thread_t thread) {
    WaitForSingleObject((HANDLE)thread, INFINITE);
    CloseHandle((HANDLE)thread);
    return 0;
}

static inline void d8_socket_close(socket_native_t sock) {
    closesocket(sock);
}

#else /* POSIX */

static inline int d8_thread_create(d8_thread_t* thread, void (*start_routine)(void*), void* arg) {
    pthread_t t;
    if (pthread_create(&t, NULL, (void*(*)(void*))start_routine, arg) != 0) {
        return -1;
    }
    *(pthread_t*)thread = t;
    return 0;
}

static inline int d8_thread_join(d8_thread_t thread) {
    pthread_join((pthread_t)thread, NULL);
    return 0;
}

static inline void d8_socket_close(socket_native_t sock) {
    close(sock);
}

#endif

/* ============================================================================
 * Internal structures
 * ============================================================================ */

typedef struct {
    socket_native_t sock;
    int running;
    d8_udp_packet_handler_t handler;
    void* user_data;
    d8_u64* recv_count;
} d8_udp_receiver_context_t;

/* ============================================================================
 * UDP Sender Implementation
 * ============================================================================ */

int d8_udp_sender_init(d8_udp_sender_t* sender) {
    if (!sender) return -1;
    
    memset(sender, 0, sizeof(d8_udp_sender_t));
    sender->socket = NULL;
    sender->initialized = 0;
    
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return -1;
    }
#endif
    
    sender->initialized = 1;
    return 0;
}

void d8_udp_sender_cleanup(d8_udp_sender_t* sender) {
    if (!sender || !sender->initialized) return;
    
    d8_udp_sender_close(sender);
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    sender->initialized = 0;
}

int d8_udp_sender_open(d8_udp_sender_t* sender, const char* host, d8_u16 port) {
    if (!sender || !host) return -1;
    
    d8_udp_sender_close(sender);
    
    /* Create socket */
    socket_native_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_NATIVE) {
        return -1;
    }
    
    sender->socket = (d8_socket_t)(void*)(size_t)sock;
#ifdef _WIN32
    strncpy_s((char*)sender->host, D8_UDP_MAX_HOST_LEN, host, D8_UDP_MAX_HOST_LEN - 1);
#else
    strncpy((char*)sender->host, host, D8_UDP_MAX_HOST_LEN - 1);
#endif
    sender->host[D8_UDP_MAX_HOST_LEN - 1] = '\0';
    sender->port = port;
    
    return 0;
}

void d8_udp_sender_close(d8_udp_sender_t* sender) {
    if (!sender || !sender->socket) return;
    
    socket_native_t sock = (socket_native_t)(size_t)(void*)sender->socket;
    d8_socket_close(sock);
    sender->socket = NULL;
}

int d8_udp_sender_send(d8_udp_sender_t* sender, const d8_udp_packet_t* packet) {
    if (!sender || !sender->socket || !packet) return -1;

    socket_native_t sock = (socket_native_t)(size_t)(void*)sender->socket;

    /* Setup destination address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(sender->port);

#ifdef _WIN32
    if (InetPtonA(AF_INET, (const char*)sender->host, &addr.sin_addr) != 1) {
        return -1;
    }
#else
    if (inet_pton(AF_INET, (const char*)sender->host, &addr.sin_addr) != 1) {
        return -1;
    }
#endif

    /* Serialize packet */
    d8_u8 buffer[D8_UDP_PACKET_SIZE];
    d8_udp_packet_serialize(packet, buffer);

    /* Send */
    int sent = sendto(sock, (const char*)buffer, D8_UDP_PACKET_SIZE, 0,
                      (struct sockaddr*)&addr, sizeof(addr));

    if (sent != D8_UDP_PACKET_SIZE) {
        return -1;
    }

    sender->sent_count++;
    return 0;
}

int d8_udp_sender_send_raw(d8_udp_sender_t* sender, const d8_u8* data, d8_size_t size) {
    if (!sender || !sender->socket || !data || size == 0) return -1;

    socket_native_t sock = (socket_native_t)(size_t)(void*)sender->socket;

    /* Setup destination address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(sender->port);

#ifdef _WIN32
    if (InetPtonA(AF_INET, (const char*)sender->host, &addr.sin_addr) != 1) {
        return -1;
    }
#else
    if (inet_pton(AF_INET, (const char*)sender->host, &addr.sin_addr) != 1) {
        return -1;
    }
#endif

    /* Send */
    int sent = sendto(sock, (const char*)data, (int)size, 0,
                      (struct sockaddr*)&addr, sizeof(addr));

    if (sent < 0) {
        return -1;
    }

    sender->sent_count++;
    return 0;
}

/* ============================================================================
 * UDP Receiver Implementation
 * ============================================================================ */

/* Receiver thread entry point */
static void d8_udp_receiver_thread_func(void* arg) {
    d8_udp_receiver_context_t* ctx = (d8_udp_receiver_context_t*)arg;
    if (!ctx) return;
    
    socket_native_t sock = ctx->sock;

    while (ctx->running) {
        d8_u8 buffer[D8_UDP_PACKET_SIZE];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        
        /* Receive with timeout */
#ifdef _WIN32
        int received = recvfrom(sock, (char*)buffer, D8_UDP_PACKET_SIZE, 0,
                                (struct sockaddr*)&from, &from_len);
        
        if (received <= 0) {
            int err = WSAGetLastError();
            if (err != WSAETIMEDOUT) {
                ctx->running = 0;
                break;
            }
            continue;
        }
#else
        int received = recvfrom(sock, (char*)buffer, D8_UDP_PACKET_SIZE, 0,
                                (struct sockaddr*)&from, &from_len);
        
        if (received <= 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ctx->running = 0;
                break;
            }
            continue;
        }
#endif
        
        /* Validate and process packet */
        if (received == D8_UDP_PACKET_SIZE) {
            d8_udp_packet_t packet;
            d8_udp_packet_deserialize(buffer, &packet);
            
            if (d8_udp_packet_is_valid(&packet) && ctx->handler) {
                (*ctx->recv_count)++;
                ctx->handler(&packet, ctx->user_data);
            }
        }
    }
}

int d8_udp_receiver_init(d8_udp_receiver_t* receiver) {
    if (!receiver) return -1;
    
    memset(receiver, 0, sizeof(d8_udp_receiver_t));
    receiver->socket = NULL;
    receiver->initialized = 0;
    
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return -1;
    }
#endif
    
    receiver->initialized = 1;
    return 0;
}

void d8_udp_receiver_cleanup(d8_udp_receiver_t* receiver) {
    if (!receiver || !receiver->initialized) return;
    
    d8_udp_receiver_stop(receiver);
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    receiver->initialized = 0;
}

int d8_udp_receiver_start(d8_udp_receiver_t* receiver, d8_u16 port,
                           d8_udp_packet_handler_t handler, void* user_data) {
    if (!receiver || !handler) return -1;
    
    d8_udp_receiver_stop(receiver);
    
    receiver->port = port;
    receiver->handler = handler;
    receiver->user_data = user_data;
    receiver->running = 1;
    
    /* Create socket */
    socket_native_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET_NATIVE) {
        receiver->running = 0;
        return -1;
    }
    
    receiver->socket = (d8_socket_t)(void*)(size_t)sock;

    /* Bind to port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        d8_socket_close(sock);
        receiver->socket = NULL;
        receiver->running = 0;
        return -1;
    }
    
    /* Set receive timeout */
#ifdef _WIN32
    {
        DWORD timeout_ms = D8_UDP_RECV_TIMEOUT_MS;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&timeout_ms, sizeof(timeout_ms));
    }
#else
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = D8_UDP_RECV_TIMEOUT_MS * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
#endif
    
    /* Create receiver context */
    d8_udp_receiver_context_t* ctx = (d8_udp_receiver_context_t*)malloc(sizeof(d8_udp_receiver_context_t));
    if (!ctx) {
        d8_socket_close(sock);
        receiver->socket = NULL;
        receiver->running = 0;
        return -1;
    }
    
    ctx->sock = sock;
    ctx->running = 1;
    ctx->handler = handler;
    ctx->user_data = user_data;
    ctx->recv_count = &receiver->recv_count;
    
    /* Start receiver thread */
    if (d8_thread_create(&receiver->thread, d8_udp_receiver_thread_func, ctx) != 0) {
        free(ctx);
        d8_socket_close(sock);
        receiver->socket = NULL;
        receiver->running = 0;
        return -1;
    }
    
    return 0;
}

void d8_udp_receiver_stop(d8_udp_receiver_t* receiver) {
    if (!receiver || !receiver->running) return;
    
    receiver->running = 0;
    
    /* Shutdown socket to wake up receiver thread */
    if (receiver->socket) {
        socket_native_t sock = (socket_native_t)(size_t)(void*)receiver->socket;
#ifdef _WIN32
        shutdown(sock, SD_BOTH);
#else
        shutdown(sock, SHUT_RDWR);
#endif
        d8_socket_close(sock);
        receiver->socket = NULL;
    }
    
    /* Wait for thread to finish */
    if (receiver->thread) {
        d8_thread_join(receiver->thread);
        receiver->thread = NULL;
    }
}

int d8_udp_receiver_is_running(const d8_udp_receiver_t* receiver) {
    return receiver ? receiver->running : 0;
}
