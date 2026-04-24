/*
 * AnnouncementPacket decoder. Mirrors Packets.swift AnnouncementPacket.
 *
 * TLV types:
 *   0x01 nickname           (UTF-8, <= 255 bytes)
 *   0x02 noise public key   (Curve25519 KeyAgreement, 32 bytes)
 *   0x03 signing public key (Ed25519, 32 bytes)
 *   0x04 direct neighbors   (8-byte peer IDs, up to 10)
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_ANNOUNCE_H
#define BITCHAT_ANNOUNCE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BC_ANNOUNCE_NICK_MAX    255
#define BC_ANNOUNCE_PUBKEY_SIZE  32
#define BC_ANNOUNCE_MAX_NEIGHBORS 10
#define BC_PEER_ID_SIZE           8

typedef struct {
    char     nickname[BC_ANNOUNCE_NICK_MAX + 1];
    size_t   nickname_len;
    uint8_t  noise_pubkey[BC_ANNOUNCE_PUBKEY_SIZE];
    uint8_t  signing_pubkey[BC_ANNOUNCE_PUBKEY_SIZE];
    uint8_t  neighbors[BC_ANNOUNCE_MAX_NEIGHBORS][BC_PEER_ID_SIZE];
    size_t   neighbor_count;
    bool     has_noise_pubkey;
    bool     has_signing_pubkey;
} bc_announce_t;

/* Returns true on success. nickname + both pubkeys are required per Swift
 * decoder; announcements missing either field return false. */
bool bc_announce_decode(const uint8_t *buf, size_t len, bc_announce_t *out);

#endif /* BITCHAT_ANNOUNCE_H */
