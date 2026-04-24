/*
 * BitchatPacket encoder — production, matches BitFoundation/BinaryProtocol.swift.
 *
 * Signing model (from NoiseEncryptionService.signPacket + toBinaryDataForSigning):
 *
 *   1. Build the packet with signature = NULL, ttl = 0, isRSR = false
 *   2. Encode it (padded) to canonical bytes
 *   3. Ed25519-sign those bytes with the sender's signing key
 *   4. Store the 64-byte signature on the packet
 *   5. Re-encode with the real ttl and the attached signature
 *
 * Receivers reverse this: strip the signature, zero ttl + isRSR, re-encode,
 * Ed25519-verify.
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_ENCODER_H
#define BITCHAT_ENCODER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "packet.h"

struct bc_identity;   /* forward — avoids including identity.h here */

typedef struct {
    uint8_t  version;         /* 1 or 2 */
    uint8_t  type;
    uint8_t  ttl;
    uint64_t timestamp;       /* ms since epoch; 0 = use current time */
    uint8_t  flags_extra;     /* e.g. BC_FLAG_IS_RSR OR'd in externally */
    const uint8_t *sender_id;                 /* 8 bytes, required */
    const uint8_t *recipient_id;              /* 8 bytes or NULL */
    const uint8_t *payload;
    size_t   payload_len;
    const uint8_t *signature; /* 64 bytes or NULL */
    const uint8_t *route_hops;  /* v2-only, 8 bytes each */
    uint8_t  route_count;
    bool     compress;        /* raw-deflate the payload if worth it */
    bool     pad_to_block;    /* PKCS#7 pad to smallest of {256,512,1024,2048} */
} bc_enc_params_t;

/* Encode the packet as-is. Returns bytes written, or 0 on error. */
size_t bc_enc_encode(const bc_enc_params_t *p, uint8_t *out, size_t cap);

/* Encode the canonical form that will be fed to Ed25519 for signing:
 *   signature = NULL, ttl = 0, isRSR stripped, otherwise identical.
 * Padding + compression follow the same rules as encode(). Callers can
 * then sign the result and pass it back through encode() with the real
 * ttl + signature filled in. */
size_t bc_enc_encode_for_signing(const bc_enc_params_t *p, uint8_t *out, size_t cap);

/* One-shot convenience: encode-for-signing → sign with `id` → re-encode
 * with signature. Returns bytes written to out_frame, or 0 on error. */
size_t bc_enc_encode_and_sign(const bc_enc_params_t *p,
                              const struct bc_identity *id,
                              uint8_t *out_frame, size_t frame_cap);

/* Raw deflate that matches Apple's COMPRESSION_ZLIB (windowBits = -15). */
size_t bc_enc_deflate_raw(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap);

#endif /* BITCHAT_ENCODER_H */
