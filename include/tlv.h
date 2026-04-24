/*
 * Simple TLV iterator for BitChat TLV-encoded payloads.
 *
 *   [type:u8][length:u8][value:length] *
 *
 * Unknown types are skipped (forward-compatible).
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_TLV_H
#define BITCHAT_TLV_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         off;
} bc_tlv_iter_t;

typedef struct {
    uint8_t        type;
    uint8_t        length;
    const uint8_t *value;
} bc_tlv_t;

void bc_tlv_iter_init(bc_tlv_iter_t *it, const uint8_t *buf, size_t len);

/* Returns true and fills *out on success. Returns false at end-of-buffer or
 * on malformed TLV. */
bool bc_tlv_next(bc_tlv_iter_t *it, bc_tlv_t *out);

#endif /* BITCHAT_TLV_H */
