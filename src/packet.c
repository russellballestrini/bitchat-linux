/*
 * BitChat packet codec implementation. See packet.h for the wire format.
 *
 * Apple's COMPRESSION_ZLIB produces raw deflate (no zlib header), so we
 * inflate with windowBits = -15.
 *
 * This is free and unencumbered software released into the public domain.
 */

#include "packet.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Compression ratio cap matches BinaryProtocol.decodeCore (50,000:1). */
#define BC_MAX_COMPRESSION_RATIO 50000.0

/* Apps don't send anything close to 64 MiB over BLE, but cap for safety. */
#define BC_MAX_PAYLOAD_BYTES (64u * 1024u * 1024u)

static uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

static uint64_t rd_be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

size_t bc_unpad(const uint8_t *data, size_t len) {
    if (len == 0) return 0;
    uint8_t last = data[len - 1];
    if (last == 0 || last > len) return len;
    for (size_t i = len - last; i < len; i++) {
        if (data[i] != last) return len;
    }
    return len - last;
}

static int bc_inflate_raw(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -15) != Z_OK) return -1;
    strm.next_in  = (Bytef *)src;
    strm.avail_in = (uInt)src_len;
    strm.next_out = dst;
    strm.avail_out = (uInt)dst_cap;
    int rc = inflate(&strm, Z_FINISH);
    size_t produced = strm.total_out;
    inflateEnd(&strm);
    if (rc != Z_STREAM_END) return -1;
    return (int)produced;
}

static bc_err_t decode_core(const uint8_t *buf, size_t len, bc_packet_t *out) {
    if (len < BC_V1_HEADER_SIZE + BC_SENDER_ID_SIZE) return BC_ERR_SHORT;

    memset(out, 0, sizeof(*out));

    size_t off = 0;
    uint8_t version = buf[off++];
    if (version != 1 && version != 2) return BC_ERR_VERSION;
    out->version = version;

    size_t header_size = (version == 2) ? BC_V2_HEADER_SIZE : BC_V1_HEADER_SIZE;
    size_t length_bytes = (version == 2) ? 4 : 2;
    if (len < header_size + BC_SENDER_ID_SIZE) return BC_ERR_SHORT;

    out->type = buf[off++];
    out->ttl  = buf[off++];

    out->timestamp = rd_be64(buf + off);
    off += 8;

    out->flags = buf[off++];

    size_t payload_len;
    if (version == 2) {
        payload_len = rd_be32(buf + off);
        off += 4;
    } else {
        payload_len = rd_be16(buf + off);
        off += 2;
    }
    if (payload_len > BC_MAX_PAYLOAD_BYTES) return BC_ERR_OVERFLOW;

    /* Sender ID */
    memcpy(out->sender_id, buf + off, BC_SENDER_ID_SIZE);
    off += BC_SENDER_ID_SIZE;

    bool has_recipient = (out->flags & BC_FLAG_HAS_RECIPIENT) != 0;
    bool has_signature = (out->flags & BC_FLAG_HAS_SIGNATURE) != 0;
    bool is_compressed = (out->flags & BC_FLAG_IS_COMPRESSED) != 0;
    bool has_route     = (version >= 2) && ((out->flags & BC_FLAG_HAS_ROUTE) != 0);

    out->has_recipient = has_recipient;
    out->has_signature = has_signature;

    if (has_recipient) {
        if (off + BC_RECIPIENT_ID_SIZE > len) return BC_ERR_SHORT;
        memcpy(out->recipient_id, buf + off, BC_RECIPIENT_ID_SIZE);
        off += BC_RECIPIENT_ID_SIZE;
    }

    if (has_route) {
        if (off + 1 > len) return BC_ERR_SHORT;
        uint8_t hops = buf[off++];
        out->route_count = hops;
        if (hops > 0) {
            size_t hops_bytes = (size_t)hops * BC_SENDER_ID_SIZE;
            if (off + hops_bytes > len) return BC_ERR_SHORT;
            out->route_hops = buf + off;
            off += hops_bytes;
        }
    }

    /* Payload */
    if (is_compressed) {
        if (payload_len < length_bytes) return BC_ERR_SHORT;
        size_t original_size = (version == 2)
            ? rd_be32(buf + off) : rd_be16(buf + off);
        off += length_bytes;
        if (original_size == 0 || original_size > BC_MAX_PAYLOAD_BYTES)
            return BC_ERR_OVERFLOW;
        size_t compressed_len = payload_len - length_bytes;
        if (off + compressed_len > len) return BC_ERR_SHORT;

        double ratio = (double)original_size / (double)compressed_len;
        if (ratio > BC_MAX_COMPRESSION_RATIO) return BC_ERR_COMPRESS;

        uint8_t *dst = (uint8_t *)malloc(original_size);
        if (!dst) return BC_ERR_INTERNAL;
        int got = bc_inflate_raw(buf + off, compressed_len, dst, original_size);
        if (got < 0 || (size_t)got != original_size) {
            free(dst);
            return BC_ERR_COMPRESS;
        }
        out->_owned_payload = dst;
        out->payload     = dst;
        out->payload_len = original_size;
        off += compressed_len;
    } else {
        if (off + payload_len > len) return BC_ERR_SHORT;
        out->payload     = buf + off;
        out->payload_len = payload_len;
        off += payload_len;
    }

    if (has_signature) {
        if (off + BC_SIGNATURE_SIZE > len) return BC_ERR_SHORT;
        memcpy(out->signature, buf + off, BC_SIGNATURE_SIZE);
        off += BC_SIGNATURE_SIZE;
    }

    return BC_OK;
}

bc_err_t bc_packet_decode(const uint8_t *data, size_t len, bc_packet_t *out) {
    /* First try as-is. */
    bc_err_t rc = decode_core(data, len, out);
    if (rc == BC_OK) return BC_OK;

    /* If that failed, strip PKCS#7 padding and retry. */
    size_t unpadded = bc_unpad(data, len);
    if (unpadded == len) {
        memset(out, 0, sizeof(*out));
        return rc;
    }
    return decode_core(data, unpadded, out);
}

void bc_packet_free(bc_packet_t *pkt) {
    if (!pkt) return;
    free(pkt->_owned_payload);
    pkt->_owned_payload = NULL;
    pkt->payload = NULL;
    pkt->payload_len = 0;
    pkt->route_hops = NULL;
}

const char *bc_msg_type_name(uint8_t type) {
    switch (type) {
    case BC_MSG_ANNOUNCE:        return "announce";
    case BC_MSG_MESSAGE:         return "message";
    case BC_MSG_LEAVE:           return "leave";
    case BC_MSG_NOISE_HANDSHAKE: return "noiseHandshake";
    case BC_MSG_NOISE_ENCRYPTED: return "noiseEncrypted";
    case BC_MSG_FRAGMENT:        return "fragment";
    case BC_MSG_REQUEST_SYNC:    return "requestSync";
    case BC_MSG_FILE_TRANSFER:   return "fileTransfer";
    default:                     return "unknown";
    }
}
