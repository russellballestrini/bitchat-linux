/*
 * Reusable byte blobs for tests.
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_TEST_FIXTURES_H
#define BITCHAT_TEST_FIXTURES_H

#include <stddef.h>
#include <stdint.h>

extern const uint8_t  FX_SENDER_ID[8];
extern const uint8_t  FX_RECIPIENT_ID[8];
extern const uint8_t  FX_NOISE_PUBKEY[32];
extern const uint8_t  FX_SIGNING_PUBKEY[32];
extern const uint8_t  FX_SIGNATURE[64];

/* Build an AnnouncementPacket TLV payload for the given nickname. Writes
 * into out (cap >= 128 bytes is safe). Returns bytes written. */
size_t fx_build_announce_tlv(const char *nickname, uint8_t *out, size_t cap);

/* Build an AnnouncementPacket with optional direct-neighbors TLV. */
size_t fx_build_announce_tlv_with_neighbors(const char *nickname,
                                            const uint8_t *neighbors_8b,
                                            size_t neighbor_count,
                                            uint8_t *out, size_t cap);

#endif /* BITCHAT_TEST_FIXTURES_H */
