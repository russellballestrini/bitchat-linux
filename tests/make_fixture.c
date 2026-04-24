/*
 * Emits a known-good BitchatPacket. Output is hex by default, or
 * length-prefixed binary (4-byte BE + frame) with --stream. Used by
 * the functional shell tests.
 *
 *   make_fixture announce            public announce for "russell"
 *   make_fixture message             public message "hello from the mesh"
 *   make_fixture padded              padded announce (block 256)
 *   make_fixture v2_routed           v2 frame with two route hops
 *   make_fixture --stream announce   same, emitted as len-prefixed binary
 *                                    suitable for | bitchat-linux --listen-stream
 *
 * This is free and unencumbered software released into the public domain.
 */

#include "encoder.h"
#include "fixtures.h"
#include "packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_stream_mode = 0;

static void emit(const uint8_t *buf, size_t n) {
    if (g_stream_mode) {
        uint8_t hdr[4] = {
            (uint8_t)((n >> 24) & 0xff),
            (uint8_t)((n >> 16) & 0xff),
            (uint8_t)((n >>  8) & 0xff),
            (uint8_t)( n        & 0xff),
        };
        fwrite(hdr, 1, 4, stdout);
        fwrite(buf, 1, n, stdout);
        fflush(stdout);
    } else {
        for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
        putchar('\n');
    }
}

/* Back-compat alias for existing call sites. */
#define emit_hex(buf, n) emit((buf), (n))

static int emit_announce(int padded) {
    uint8_t tlv[128];
    size_t tlv_len = fx_build_announce_tlv("russell", tlv, sizeof(tlv));
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_ANNOUNCE, .ttl = 7,
        .timestamp = 0x0000018e12345678ULL,
        .sender_id = FX_SENDER_ID,
        .payload = tlv, .payload_len = tlv_len,
        .pad_to_block = padded ? true : false,
    };
    uint8_t frame[2048];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    if (n == 0) return 1;
    emit_hex(frame, n);
    return 0;
}

static int emit_message(void) {
    const char *msg = "hello from the mesh";
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = 5, .timestamp = 1,
        .sender_id = FX_SENDER_ID,
        .payload = (const uint8_t *)msg, .payload_len = strlen(msg),
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    if (n == 0) return 1;
    emit_hex(frame, n);
    return 0;
}

static int emit_v2_routed(void) {
    const uint8_t hops[] = {
        0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
        0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,
    };
    const char *msg = "two hops";
    bc_enc_params_t p = {
        .version = 2, .type = BC_MSG_MESSAGE, .ttl = 3, .timestamp = 2,
        .sender_id = FX_SENDER_ID,
        .payload = (const uint8_t *)msg, .payload_len = strlen(msg),
        .route_hops = hops, .route_count = 2,
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    if (n == 0) return 1;
    emit_hex(frame, n);
    return 0;
}

int main(int argc, char **argv) {
    int i = 1;
    if (i < argc && strcmp(argv[i], "--stream") == 0) { g_stream_mode = 1; i++; }
    if (i >= argc) {
        fprintf(stderr, "usage: %s [--stream] {announce|message|padded|v2_routed}\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[i], "announce")   == 0) return emit_announce(0);
    if (strcmp(argv[i], "padded")     == 0) return emit_announce(1);
    if (strcmp(argv[i], "message")    == 0) return emit_message();
    if (strcmp(argv[i], "v2_routed")  == 0) return emit_v2_routed();
    fprintf(stderr, "unknown fixture: %s\n", argv[i]);
    return 2;
}
