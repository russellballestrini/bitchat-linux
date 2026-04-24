/*
 * BitChat packet codec for Linux client.
 *
 * Mirrors BitFoundation/BinaryProtocol.swift wire format:
 *
 *   v1 header (14 bytes): version(1) type(1) ttl(1) timestamp(8) flags(1) payloadLen(2)
 *   v2 header (16 bytes): version(1) type(1) ttl(1) timestamp(8) flags(1) payloadLen(4)
 *
 *   Then: senderID(8), optional recipientID(8), optional route(v2 only),
 *         optional compression-preamble, payload, optional signature(64).
 *
 *   PKCS#7-ish trailing padding to {256,512,1024,2048} may be applied.
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_PACKET_H
#define BITCHAT_PACKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BC_SENDER_ID_SIZE     8
#define BC_RECIPIENT_ID_SIZE  8
#define BC_SIGNATURE_SIZE    64
#define BC_V1_HEADER_SIZE    14
#define BC_V2_HEADER_SIZE    16

/* Flag bits (BinaryProtocol.Flags) */
#define BC_FLAG_HAS_RECIPIENT 0x01
#define BC_FLAG_HAS_SIGNATURE 0x02
#define BC_FLAG_IS_COMPRESSED 0x04
#define BC_FLAG_HAS_ROUTE     0x08
#define BC_FLAG_IS_RSR        0x10

/* Message types (BitFoundation/MessageType.swift) */
#define BC_MSG_ANNOUNCE        0x01
#define BC_MSG_MESSAGE         0x02
#define BC_MSG_LEAVE           0x03
#define BC_MSG_NOISE_HANDSHAKE 0x10
#define BC_MSG_NOISE_ENCRYPTED 0x11
#define BC_MSG_FRAGMENT        0x20
#define BC_MSG_REQUEST_SYNC    0x21
#define BC_MSG_FILE_TRANSFER   0x22

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  ttl;
    uint64_t timestamp;    /* ms since epoch */
    uint8_t  flags;

    uint8_t  sender_id[BC_SENDER_ID_SIZE];

    bool     has_recipient;
    uint8_t  recipient_id[BC_RECIPIENT_ID_SIZE];

    /* Route (v2 only): up to 255 hops of 8 bytes each. Borrowed pointer
     * into the decoded-packet buffer when parsing; caller owns lifetime. */
    uint8_t  route_count;
    const uint8_t *route_hops;

    /* Payload is a borrowed pointer into an internal buffer owned by the
     * packet (freed by bc_packet_free). For compressed packets it points
     * at the decompressed bytes. */
    const uint8_t *payload;
    size_t   payload_len;

    bool     has_signature;
    uint8_t  signature[BC_SIGNATURE_SIZE];

    /* Internal: decompression buffer, owned. NULL if payload points
     * directly into the input frame. */
    uint8_t *_owned_payload;
} bc_packet_t;

typedef enum {
    BC_OK = 0,
    BC_ERR_SHORT     = -1,
    BC_ERR_VERSION   = -2,
    BC_ERR_OVERFLOW  = -3,
    BC_ERR_COMPRESS  = -4,
    BC_ERR_INTERNAL  = -5,
} bc_err_t;

/* Decode a BLE frame (may be padded). Returns BC_OK on success; caller must
 * call bc_packet_free on *out when done. On error *out is zeroed. */
bc_err_t bc_packet_decode(const uint8_t *data, size_t len, bc_packet_t *out);

void bc_packet_free(bc_packet_t *pkt);

/* Strip PKCS#7-style trailing pad. Returns the unpadded length. If the
 * trailing byte doesn't describe valid padding, returns len unchanged. */
size_t bc_unpad(const uint8_t *data, size_t len);

const char *bc_msg_type_name(uint8_t type);

#endif /* BITCHAT_PACKET_H */
