/*
 * Test-only BitchatPacket encoder.
 *
 * Lives under tests/ rather than src/ because Stage 3 (send path) will
 * ship its own production encoder. This helper exists so unit tests can
 * construct valid (and deliberately invalid) frames to feed the decoder.
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_TEST_ENCODER_H
#define BITCHAT_TEST_ENCODER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "packet.h"

typedef struct {
    uint8_t  version;         /* 1 or 2 */
    uint8_t  type;
    uint8_t  ttl;
    uint64_t timestamp;
    uint8_t  flags_extra;     /* extra bits OR-ed onto computed flags (eg. isRSR) */
    const uint8_t *sender_id;                 /* 8 bytes, required */
    const uint8_t *recipient_id;              /* 8 bytes or NULL */
    const uint8_t *payload;
    size_t   payload_len;
    const uint8_t *signature; /* 64 bytes or NULL */
    /* v2-only route hops (each 8 bytes). NULL or hop_count=0 for none. */
    const uint8_t *route_hops;
    uint8_t  route_count;
    bool     compress;        /* apply raw-deflate to payload */
    bool     pad_to_block;    /* PKCS#7 pad to the smallest {256,512,1024,2048} */
} bc_enc_params_t;

/* Encode into out (cap bytes). Returns the number of bytes written, or 0
 * on error (overflow, invalid inputs). */
size_t bc_enc_encode(const bc_enc_params_t *p, uint8_t *out, size_t cap);

/* Helper: raw deflate (matches COMPRESSION_ZLIB on Apple, decodeable by
 * bc_packet_decode via windowBits=-15). */
size_t bc_enc_deflate_raw(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap);

#endif /* BITCHAT_TEST_ENCODER_H */
