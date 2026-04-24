/*
 * bitchat-linux — a C client for the BitChat mesh.
 *
 * Stage 1: decodes BitchatPacket frames.
 * Stage 2: BLE receive-only observer via BlueZ (sd-bus).
 *
 * Usage:
 *   bitchat-linux --decode          Read hex on stdin and print decoded packet.
 *   bitchat-linux --listen          Join the mesh; print each received packet.
 *   bitchat-linux --self-test       Run built-in round-trip tests.
 *
 * This is free and unencumbered software released into the public domain.
 */

#include "announce.h"
#include "ble.h"
#include "hex.h"
#include "packet.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAME_BYTES (256u * 1024u)

static void print_bytes(const uint8_t *b, size_t n) {
    char tmp[256];
    size_t chunk = n > 64 ? 64 : n;
    bc_hex_encode(b, chunk, tmp);
    fputs(tmp, stdout);
    if (n > 64) printf("... (%zu bytes total)", n);
}

static void print_packet(const bc_packet_t *pkt) {
    printf("BitchatPacket v%u\n", pkt->version);
    printf("  type       : 0x%02x (%s)\n",
           pkt->type, bc_msg_type_name(pkt->type));
    printf("  ttl        : %u\n", pkt->ttl);
    printf("  timestamp  : %llu ms\n", (unsigned long long)pkt->timestamp);
    printf("  flags      : 0x%02x", pkt->flags);
    if (pkt->flags) {
        printf(" (");
        int any = 0;
        if (pkt->flags & BC_FLAG_HAS_RECIPIENT) { printf("%srecipient", any++ ? "," : ""); }
        if (pkt->flags & BC_FLAG_HAS_SIGNATURE) { printf("%ssignature", any++ ? "," : ""); }
        if (pkt->flags & BC_FLAG_IS_COMPRESSED) { printf("%scompressed", any++ ? "," : ""); }
        if (pkt->flags & BC_FLAG_HAS_ROUTE)     { printf("%sroute",     any++ ? "," : ""); }
        if (pkt->flags & BC_FLAG_IS_RSR)        { printf("%srsr",       any++ ? "," : ""); }
        printf(")");
    }
    putchar('\n');

    char hex[BC_SIGNATURE_SIZE * 2 + 1];
    bc_hex_encode(pkt->sender_id, BC_SENDER_ID_SIZE, hex);
    printf("  sender     : %s\n", hex);
    if (pkt->has_recipient) {
        bc_hex_encode(pkt->recipient_id, BC_RECIPIENT_ID_SIZE, hex);
        printf("  recipient  : %s\n", hex);
    }
    if (pkt->route_count > 0) {
        printf("  route      : %u hops\n", pkt->route_count);
        for (uint8_t i = 0; i < pkt->route_count; i++) {
            bc_hex_encode(pkt->route_hops + i * BC_SENDER_ID_SIZE,
                          BC_SENDER_ID_SIZE, hex);
            printf("    hop[%u] : %s\n", i, hex);
        }
    }
    printf("  payload    : %zu bytes : ", pkt->payload_len);
    print_bytes(pkt->payload, pkt->payload_len);
    putchar('\n');

    if (pkt->has_signature) {
        bc_hex_encode(pkt->signature, BC_SIGNATURE_SIZE, hex);
        printf("  signature  : %s\n", hex);
    }

    /* Type-aware payload dump. */
    switch (pkt->type) {
    case BC_MSG_ANNOUNCE: {
        bc_announce_t ann;
        if (bc_announce_decode(pkt->payload, pkt->payload_len, &ann)) {
            printf("  announce   : nickname=\"%s\"\n", ann.nickname);
            bc_hex_encode(ann.noise_pubkey, BC_ANNOUNCE_PUBKEY_SIZE, hex);
            printf("               noise_pubkey=%s\n", hex);
            bc_hex_encode(ann.signing_pubkey, BC_ANNOUNCE_PUBKEY_SIZE, hex);
            printf("               signing_pubkey=%s\n", hex);
            if (ann.neighbor_count > 0) {
                printf("               neighbors=%zu\n", ann.neighbor_count);
            }
        } else {
            printf("  announce   : <malformed>\n");
        }
        break;
    }
    case BC_MSG_MESSAGE: {
        /* Raw UTF-8 text per BLEService.swift:444. */
        printf("  text       : ");
        fwrite(pkt->payload, 1, pkt->payload_len, stdout);
        putchar('\n');
        break;
    }
    default:
        break;
    }
}

static int cmd_decode(void) {
    /* Slurp stdin. */
    static char buf[2 * MAX_FRAME_BYTES + 16];
    size_t used = 0;
    int c;
    while ((c = getchar()) != EOF) {
        if (used + 1 >= sizeof(buf)) {
            fprintf(stderr, "input too large (> %u bytes of hex)\n", (unsigned)sizeof(buf));
            return 1;
        }
        buf[used++] = (char)c;
    }
    buf[used] = '\0';

    uint8_t *frame = (uint8_t *)malloc(MAX_FRAME_BYTES);
    if (!frame) { fprintf(stderr, "oom\n"); return 1; }

    long n = bc_hex_decode(buf, frame, MAX_FRAME_BYTES);
    if (n < 0) {
        fprintf(stderr, "invalid hex input\n");
        free(frame);
        return 1;
    }

    bc_packet_t pkt;
    bc_err_t rc = bc_packet_decode(frame, (size_t)n, &pkt);
    if (rc != BC_OK) {
        fprintf(stderr, "decode failed: %d\n", rc);
        free(frame);
        return 1;
    }
    print_packet(&pkt);
    bc_packet_free(&pkt);
    free(frame);
    return 0;
}

/* --- self-test: hand-encode a v1 packet, decode it, verify fields. --- */

static void put_be16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }
static void put_be64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = v & 0xff; v >>= 8; }
}

static int self_test(void) {
    /* Public announce packet, v1, no recipient, no signature. */
    uint8_t sender[8] = { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x00, 0x01 };
    uint8_t noise_k[32], sign_k[32];
    for (int i = 0; i < 32; i++) { noise_k[i] = (uint8_t)(0x10 + i); sign_k[i] = (uint8_t)(0x40 + i); }
    const char *nick = "fox";
    uint8_t tlv[3 + 3 + 3 + 32 + 32];
    size_t off = 0;
    tlv[off++] = 0x01; tlv[off++] = (uint8_t)strlen(nick);
    memcpy(tlv + off, nick, strlen(nick)); off += strlen(nick);
    tlv[off++] = 0x02; tlv[off++] = 32; memcpy(tlv + off, noise_k, 32); off += 32;
    tlv[off++] = 0x03; tlv[off++] = 32; memcpy(tlv + off, sign_k,  32); off += 32;

    uint8_t frame[256];
    size_t fi = 0;
    frame[fi++] = 0x01;                 /* version 1 */
    frame[fi++] = BC_MSG_ANNOUNCE;      /* type      */
    frame[fi++] = 7;                    /* ttl       */
    put_be64(frame + fi, 0x0000018e0000ffffULL); fi += 8;
    frame[fi++] = 0x00;                 /* flags     */
    put_be16(frame + fi, (uint16_t)off); fi += 2;
    memcpy(frame + fi, sender, 8); fi += 8;
    memcpy(frame + fi, tlv, off); fi += off;

    bc_packet_t pkt;
    bc_err_t rc = bc_packet_decode(frame, fi, &pkt);
    if (rc != BC_OK) { fprintf(stderr, "self-test decode rc=%d\n", rc); return 1; }
    if (pkt.type != BC_MSG_ANNOUNCE)                    { fprintf(stderr, "bad type\n"); return 1; }
    if (pkt.ttl != 7)                                    { fprintf(stderr, "bad ttl\n"); return 1; }
    if (memcmp(pkt.sender_id, sender, 8) != 0)           { fprintf(stderr, "bad sender\n"); return 1; }
    if (pkt.has_recipient || pkt.has_signature)          { fprintf(stderr, "unexpected flags\n"); return 1; }

    bc_announce_t ann;
    if (!bc_announce_decode(pkt.payload, pkt.payload_len, &ann)) {
        fprintf(stderr, "announce decode failed\n"); return 1;
    }
    if (strcmp(ann.nickname, "fox") != 0)                { fprintf(stderr, "bad nick\n"); return 1; }
    if (memcmp(ann.noise_pubkey, noise_k, 32) != 0)      { fprintf(stderr, "bad noise key\n"); return 1; }
    if (memcmp(ann.signing_pubkey, sign_k, 32) != 0)     { fprintf(stderr, "bad sign key\n"); return 1; }

    bc_packet_free(&pkt);

    /* Test padding strip: pad the above frame to 256 bytes with PKCS#7, decode. */
    uint8_t padded[256];
    memcpy(padded, frame, fi);
    uint8_t pad = (uint8_t)(256 - fi);
    memset(padded + fi, pad, pad);
    rc = bc_packet_decode(padded, 256, &pkt);
    if (rc != BC_OK) { fprintf(stderr, "padded decode rc=%d\n", rc); return 1; }
    if (pkt.type != BC_MSG_ANNOUNCE) { fprintf(stderr, "padded bad type\n"); return 1; }
    bc_packet_free(&pkt);

    printf("self-test: OK\n");
    return 0;
}

/* --- listen: BLE mesh observer --- */

static bc_ble_ctx_t *g_ble_ctx = NULL;

static void on_sigint(int sig) { (void)sig; bc_ble_stop(g_ble_ctx); }

static void on_frame(const uint8_t *data, size_t len, const char *peer_path, void *user) {
    (void)user;
    bc_packet_t pkt;
    bc_err_t rc = bc_packet_decode(data, len, &pkt);
    if (rc != BC_OK) {
        fprintf(stderr, "[%s] decode failed rc=%d (len=%zu)\n",
                peer_path ? peer_path : "?", rc, len);
        return;
    }
    printf("--- frame from %s (%zu bytes) ---\n",
           peer_path ? peer_path : "?", len);
    print_packet(&pkt);
    putchar('\n');
    fflush(stdout);
    bc_packet_free(&pkt);
}

static void on_peer(const char *peer_path, const char *event, void *user) {
    (void)user;
    fprintf(stderr, "[peer] %s: %s\n", event, peer_path ? peer_path : "?");
}

static int cmd_listen(int use_testnet) {
    g_ble_ctx = bc_ble_new(on_frame, on_peer, NULL);
    if (!g_ble_ctx) { fprintf(stderr, "ble init failed\n"); return 1; }

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    int r = bc_ble_start(g_ble_ctx, use_testnet);
    if (r < 0) {
        fprintf(stderr, "ble start failed: %d\n", r);
        bc_ble_free(g_ble_ctx);
        return 1;
    }

    r = bc_ble_run(g_ble_ctx);
    bc_ble_free(g_ble_ctx);
    g_ble_ctx = NULL;
    return r < 0 ? 1 : 0;
}

static void usage(void) {
    fputs(
        "bitchat-linux — C client for the BitChat mesh\n"
        "Usage:\n"
        "  bitchat-linux --decode       Decode hex BitchatPacket frame on stdin\n"
        "  bitchat-linux --listen       Join the mesh and print received packets\n"
        "  bitchat-linux --listen-test  Listen on the testnet service UUID\n"
        "  bitchat-linux --self-test    Run built-in round-trip tests\n"
        "  bitchat-linux --help         Show this help\n",
        stderr);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    if (strcmp(argv[1], "--decode") == 0)      return cmd_decode();
    if (strcmp(argv[1], "--listen") == 0)      return cmd_listen(0);
    if (strcmp(argv[1], "--listen-test") == 0) return cmd_listen(1);
    if (strcmp(argv[1], "--self-test") == 0)   return self_test();
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return 0;
    }
    usage();
    return 2;
}
