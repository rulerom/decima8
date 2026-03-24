/*
 * DECIMA-8 Core - Pure C Implementation
 * UDP Protocol for network telemetry
 *
 * All rights belong to the ORDEN (c) 2026
 */

#pragma once

#include "d8/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define D8_UDP_MAGIC       0x50553844U  /* 'D8UP' little-endian */
#define D8_UDP_VERSION     1U
#define D8_UDP_PACKET_SIZE 37U

/* UDP packet flags */
#define D8_UDP_FLAG_NONE       0x0000U
#define D8_UDP_FLAG_HAS_WINNER 0x0001U
#define D8_UDP_FLAG_HAS_BUS    0x0002U
#define D8_UDP_FLAG_HAS_CYCLE  0x0004U
#define D8_UDP_FLAG_HAS_FLAGS  0x0008U

/* ============================================================================
 * UDP Packet structure (packed, 37 bytes)
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct {
    d8_u32 magic;          /* 0x50553844 'D8UP' */
    d8_u16 version;        /* 1 */
    d8_u16 flags;          /* bit0=has_winner, bit1=has_bus, bit2=has_cycle, bit3=has_flags */

    d8_u32 frame_tag;      /* Frame number/tag */
    d8_u8  domain_id;      /* Domain ID (0..15, 0xFF if none) */
    d8_u16 pattern_id;     /* Pattern ID */
    d8_u16 reset_mask16;   /* Reset mask */
    d8_u16 collision_mask16; /* Collision mask */
    d8_u16 winner_tile_id; /* Winner tile ID (0xFFFF if none) */
    d8_u32 cycle_time_us;  /* Cycle time in microseconds */
    d8_u32 flags32_last;   /* Last flags32 */

    d8_u8  bus16[8];       /* BUS16 values (8 lanes) */
} d8_udp_packet_t;
#pragma pack(pop)

/* ============================================================================
 * Packet construction
 * ============================================================================ */

/**
 * @brief Initialize UDP packet with default values
 * @param pkt Pointer to packet
 */
static inline void d8_udp_packet_init(d8_udp_packet_t* pkt) {
    pkt->magic = D8_UDP_MAGIC;
    pkt->version = D8_UDP_VERSION;
    pkt->flags = D8_UDP_FLAG_NONE;
    pkt->frame_tag = 0;
    pkt->domain_id = 0xFF;
    pkt->pattern_id = 0;
    pkt->reset_mask16 = 0xFFFF;
    pkt->collision_mask16 = 0;
    pkt->winner_tile_id = 0xFFFF;
    pkt->cycle_time_us = 0;
    pkt->flags32_last = 0;
    for (int i = 0; i < 8; i++) {
        pkt->bus16[i] = 0;
    }
}

/**
 * @brief Set packet flags based on what data is present
 * @param pkt Pointer to packet
 * @param has_winner true if winner data is present
 * @param has_bus true if bus data is present
 * @param has_cycle true if cycle time is present
 * @param has_flags true if flags32 is present
 */
static inline void d8_udp_packet_set_flags(d8_udp_packet_t* pkt,
                                            int has_winner, int has_bus,
                                            int has_cycle, int has_flags) {
    pkt->flags = 0;
    if (has_winner) pkt->flags |= D8_UDP_FLAG_HAS_WINNER;
    if (has_bus)    pkt->flags |= D8_UDP_FLAG_HAS_BUS;
    if (has_cycle)  pkt->flags |= D8_UDP_FLAG_HAS_CYCLE;
    if (has_flags)  pkt->flags |= D8_UDP_FLAG_HAS_FLAGS;
}

/**
 * @brief Validate UDP packet header
 * @param pkt Pointer to packet
 * @return true if valid
 */
static inline int d8_udp_packet_is_valid(const d8_udp_packet_t* pkt) {
    return (pkt->magic == D8_UDP_MAGIC) && (pkt->version == D8_UDP_VERSION);
}

/**
 * @brief Serialize packet to wire format (little-endian)
 * @param pkt Pointer to packet
 * @param out_buf Output buffer (must be at least D8_UDP_PACKET_SIZE bytes)
 */
static inline void d8_udp_packet_serialize(const d8_udp_packet_t* pkt, d8_u8* out_buf) {
    /* Little-endian serialization */
    out_buf[0] = (d8_u8)(pkt->magic & 0xFF);
    out_buf[1] = (d8_u8)((pkt->magic >> 8) & 0xFF);
    out_buf[2] = (d8_u8)((pkt->magic >> 16) & 0xFF);
    out_buf[3] = (d8_u8)((pkt->magic >> 24) & 0xFF);

    out_buf[4] = (d8_u8)(pkt->version & 0xFF);
    out_buf[5] = (d8_u8)((pkt->version >> 8) & 0xFF);

    out_buf[6] = (d8_u8)(pkt->flags & 0xFF);
    out_buf[7] = (d8_u8)((pkt->flags >> 8) & 0xFF);

    out_buf[8]  = (d8_u8)(pkt->frame_tag & 0xFF);
    out_buf[9]  = (d8_u8)((pkt->frame_tag >> 8) & 0xFF);
    out_buf[10] = (d8_u8)((pkt->frame_tag >> 16) & 0xFF);
    out_buf[11] = (d8_u8)((pkt->frame_tag >> 24) & 0xFF);

    out_buf[12] = pkt->domain_id;
    out_buf[13] = (d8_u8)(pkt->pattern_id & 0xFF);
    out_buf[14] = (d8_u8)((pkt->pattern_id >> 8) & 0xFF);

    out_buf[15] = (d8_u8)(pkt->reset_mask16 & 0xFF);
    out_buf[16] = (d8_u8)((pkt->reset_mask16 >> 8) & 0xFF);

    out_buf[17] = (d8_u8)(pkt->collision_mask16 & 0xFF);
    out_buf[18] = (d8_u8)((pkt->collision_mask16 >> 8) & 0xFF);

    out_buf[19] = (d8_u8)(pkt->winner_tile_id & 0xFF);
    out_buf[20] = (d8_u8)((pkt->winner_tile_id >> 8) & 0xFF);

    out_buf[21] = (d8_u8)(pkt->cycle_time_us & 0xFF);
    out_buf[22] = (d8_u8)((pkt->cycle_time_us >> 8) & 0xFF);
    out_buf[23] = (d8_u8)((pkt->cycle_time_us >> 16) & 0xFF);
    out_buf[24] = (d8_u8)((pkt->cycle_time_us >> 24) & 0xFF);

    out_buf[25] = (d8_u8)(pkt->flags32_last & 0xFF);
    out_buf[26] = (d8_u8)((pkt->flags32_last >> 8) & 0xFF);
    out_buf[27] = (d8_u8)((pkt->flags32_last >> 16) & 0xFF);
    out_buf[28] = (d8_u8)((pkt->flags32_last >> 24) & 0xFF);

    for (int i = 0; i < 8; i++) {
        out_buf[29 + i] = pkt->bus16[i];
    }
}

/**
 * @brief Deserialize packet from wire format (little-endian)
 * @param in_buf Input buffer (at least D8_UDP_PACKET_SIZE bytes)
 * @param pkt Output packet
 */
static inline void d8_udp_packet_deserialize(const d8_u8* in_buf, d8_udp_packet_t* pkt) {
    pkt->magic = (d8_u32)in_buf[0] | ((d8_u32)in_buf[1] << 8) |
                 ((d8_u32)in_buf[2] << 16) | ((d8_u32)in_buf[3] << 24);

    pkt->version = (d8_u16)in_buf[4] | ((d8_u16)in_buf[5] << 8);

    pkt->flags = (d8_u16)in_buf[6] | ((d8_u16)in_buf[7] << 8);

    pkt->frame_tag = (d8_u32)in_buf[8] | ((d8_u32)in_buf[9] << 8) |
                     ((d8_u32)in_buf[10] << 16) | ((d8_u32)in_buf[11] << 24);

    pkt->domain_id = in_buf[12];

    pkt->pattern_id = (d8_u16)in_buf[13] | ((d8_u16)in_buf[14] << 8);

    pkt->reset_mask16 = (d8_u16)in_buf[15] | ((d8_u16)in_buf[16] << 8);

    pkt->collision_mask16 = (d8_u16)in_buf[17] | ((d8_u16)in_buf[18] << 8);

    pkt->winner_tile_id = (d8_u16)in_buf[19] | ((d8_u16)in_buf[20] << 8);

    pkt->cycle_time_us = (d8_u32)in_buf[21] | ((d8_u32)in_buf[22] << 8) |
                         ((d8_u32)in_buf[23] << 16) | ((d8_u32)in_buf[24] << 24);

    pkt->flags32_last = (d8_u32)in_buf[25] | ((d8_u32)in_buf[26] << 8) |
                        ((d8_u32)in_buf[27] << 16) | ((d8_u32)in_buf[28] << 24);

    for (int i = 0; i < 8; i++) {
        pkt->bus16[i] = in_buf[29 + i];
    }
}

#ifdef __cplusplus
}
#endif
