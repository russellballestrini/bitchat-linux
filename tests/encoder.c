#include "encoder.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

size_t bc_enc_deflate_raw(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t dst_cap) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) return 0;
    strm.next_in = (Bytef *)src;
    strm.avail_in = (uInt)src_len;
    strm.next_out = dst;
    strm.avail_out = (uInt)dst_cap;
    int rc = deflate(&strm, Z_FINISH);
    size_t produced = strm.total_out;
    deflateEnd(&strm);
    return rc == Z_STREAM_END ? produced : 0;
}

static void wr_be16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }
static void wr_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = (v >> 16) & 0xff;
    p[2] = (v >>  8) & 0xff; p[3] = v & 0xff;
}
static void wr_be64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = v & 0xff; v >>= 8; }
}

/* Smallest block in {256,512,1024,2048} that accommodates data+16 overhead. */
static size_t optimal_block_size(size_t frame_size) {
    static const size_t blocks[] = { 256, 512, 1024, 2048 };
    size_t total = frame_size + 16;
    for (size_t i = 0; i < sizeof(blocks)/sizeof(*blocks); i++) {
        if (total <= blocks[i]) return blocks[i];
    }
    return frame_size; /* no pad for very large */
}

size_t bc_enc_encode(const bc_enc_params_t *p, uint8_t *out, size_t cap) {
    if (!p || !p->sender_id || (p->version != 1 && p->version != 2)) return 0;

    size_t header_size   = (p->version == 2) ? BC_V2_HEADER_SIZE : BC_V1_HEADER_SIZE;
    size_t length_bytes  = (p->version == 2) ? 4 : 2;

    /* Optionally compress payload. */
    uint8_t *comp_buf = NULL;
    size_t   comp_len = 0;
    size_t   original_size_bytes = p->payload_len;
    int      is_compressed = 0;

    const uint8_t *payload_ptr = p->payload;
    size_t payload_out_len = p->payload_len;

    if (p->compress && p->payload_len > 0) {
        size_t comp_cap = p->payload_len + p->payload_len / 255 + 16;
        comp_buf = (uint8_t *)malloc(comp_cap);
        if (!comp_buf) return 0;
        comp_len = bc_enc_deflate_raw(p->payload, p->payload_len, comp_buf, comp_cap);
        if (comp_len == 0 || comp_len >= p->payload_len) {
            free(comp_buf);
            comp_buf = NULL;
        } else {
            is_compressed = 1;
            payload_ptr = comp_buf;
            payload_out_len = comp_len;
        }
    }

    /* payloadLength in header = length_bytes (original size prefix) + compressed_bytes
     * when compressed, otherwise just payload_out_len. */
    size_t payload_data_size = is_compressed
        ? length_bytes + payload_out_len
        : payload_out_len;

    if (p->version == 1 && payload_data_size > 0xFFFFu) { free(comp_buf); return 0; }

    uint8_t flags = p->flags_extra;
    if (p->recipient_id) flags |= BC_FLAG_HAS_RECIPIENT;
    if (p->signature)    flags |= BC_FLAG_HAS_SIGNATURE;
    if (is_compressed)   flags |= BC_FLAG_IS_COMPRESSED;
    int has_route = (p->version >= 2 && p->route_count > 0 && p->route_hops);
    if (has_route) flags |= BC_FLAG_HAS_ROUTE;

    size_t route_len = has_route ? (1 + (size_t)p->route_count * BC_SENDER_ID_SIZE) : 0;
    size_t sig_len   = p->signature ? BC_SIGNATURE_SIZE : 0;
    size_t recip_len = p->recipient_id ? BC_RECIPIENT_ID_SIZE : 0;

    size_t total = header_size + BC_SENDER_ID_SIZE + recip_len + route_len
                 + payload_data_size + sig_len;
    if (total > cap) { free(comp_buf); return 0; }

    size_t off = 0;
    out[off++] = p->version;
    out[off++] = p->type;
    out[off++] = p->ttl;
    wr_be64(out + off, p->timestamp); off += 8;
    out[off++] = flags;
    if (p->version == 2) { wr_be32(out + off, (uint32_t)payload_data_size); off += 4; }
    else                 { wr_be16(out + off, (uint16_t)payload_data_size); off += 2; }

    memcpy(out + off, p->sender_id, BC_SENDER_ID_SIZE); off += BC_SENDER_ID_SIZE;
    if (p->recipient_id) { memcpy(out + off, p->recipient_id, BC_RECIPIENT_ID_SIZE); off += BC_RECIPIENT_ID_SIZE; }
    if (has_route) {
        out[off++] = p->route_count;
        memcpy(out + off, p->route_hops, (size_t)p->route_count * BC_SENDER_ID_SIZE);
        off += (size_t)p->route_count * BC_SENDER_ID_SIZE;
    }
    if (is_compressed) {
        if (p->version == 2) { wr_be32(out + off, (uint32_t)original_size_bytes); off += 4; }
        else                 { wr_be16(out + off, (uint16_t)original_size_bytes); off += 2; }
    }
    if (payload_out_len > 0) {
        memcpy(out + off, payload_ptr, payload_out_len); off += payload_out_len;
    }
    if (p->signature) { memcpy(out + off, p->signature, BC_SIGNATURE_SIZE); off += BC_SIGNATURE_SIZE; }

    free(comp_buf);

    if (p->pad_to_block) {
        size_t block = optimal_block_size(off);
        if (block > off && block - off <= 255 && block <= cap) {
            uint8_t pad = (uint8_t)(block - off);
            memset(out + off, pad, pad);
            off += pad;
        }
    }

    return off;
}
