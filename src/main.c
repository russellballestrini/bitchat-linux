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
#include "dedup.h"
#include "encoder.h"
#include "hex.h"
#include "identity.h"
#include "packet.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

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

static int cmd_listen(int use_testnet, const char *adapter) {
    g_ble_ctx = bc_ble_new(on_frame, on_peer, NULL);
    if (!g_ble_ctx) { fprintf(stderr, "ble init failed\n"); return 1; }

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    int r = bc_ble_start(g_ble_ctx, adapter, use_testnet);
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

/* --- listen-stream: software mock, reads length-prefixed frames from stdin --- */

static int cmd_listen_stream(void) {
    /* Each frame on stdin is a 4-byte big-endian length followed by that
     * many bytes of BitchatPacket. Lets tests feed BLE-shaped data into
     * the same on_frame dispatch path that the sd-bus transport uses,
     * without touching a radio. */
    for (;;) {
        uint8_t lenbuf[4];
        size_t got = fread(lenbuf, 1, 4, stdin);
        if (got == 0) break;
        if (got != 4) {
            fprintf(stderr, "[stream] short length header (%zu bytes)\n", got);
            return 1;
        }
        uint32_t len = ((uint32_t)lenbuf[0] << 24) | ((uint32_t)lenbuf[1] << 16)
                     | ((uint32_t)lenbuf[2] <<  8) |  (uint32_t)lenbuf[3];
        if (len == 0 || len > MAX_FRAME_BYTES) {
            fprintf(stderr, "[stream] bogus frame length %u\n", len);
            return 1;
        }
        uint8_t *buf = (uint8_t *)malloc(len);
        if (!buf) { fprintf(stderr, "[stream] oom\n"); return 1; }
        if (fread(buf, 1, len, stdin) != len) {
            fprintf(stderr, "[stream] short frame body\n");
            free(buf);
            return 1;
        }
        on_frame(buf, len, "stream", NULL);
        free(buf);
    }
    return 0;
}

/* --- chat + announce: dual-role mesh participation --- */

typedef struct {
    bc_identity_t  id;
    bc_dedup_t    *dedup;
    bc_ble_ctx_t  *ble;
    const char    *nickname;
    int            use_testnet;
    int            relay;
    uint64_t       replay_at_ms;   /* 0 = no pending replay */
} chat_ctx_t;

static chat_ctx_t g_chat;

/* Queue of recent outbound frames — re-sent when a new subscriber attaches,
 * so messages typed before the peer joined aren't lost. */
#define BC_SEND_QUEUE_DEPTH 16
typedef struct {
    uint8_t buf[1024];
    size_t  len;
} sent_frame_t;
static sent_frame_t sent_queue[BC_SEND_QUEUE_DEPTH];
static int sent_queue_head = 0;  /* next slot to overwrite */
static int sent_queue_count = 0;

static void sent_queue_push(const uint8_t *data, size_t len) {
    if (len == 0 || len > sizeof(sent_queue[0].buf)) return;
    sent_frame_t *slot = &sent_queue[sent_queue_head];
    memcpy(slot->buf, data, len);
    slot->len = len;
    sent_queue_head = (sent_queue_head + 1) % BC_SEND_QUEUE_DEPTH;
    if (sent_queue_count < BC_SEND_QUEUE_DEPTH) sent_queue_count++;
}

static void sent_queue_replay(bc_ble_ctx_t *ble) {
    /* Replay in chronological order: oldest first. */
    int start = (sent_queue_head - sent_queue_count + BC_SEND_QUEUE_DEPTH)
                % BC_SEND_QUEUE_DEPTH;
    for (int i = 0; i < sent_queue_count; i++) {
        int idx = (start + i) % BC_SEND_QUEUE_DEPTH;
        bc_ble_broadcast(ble, sent_queue[idx].buf, sent_queue[idx].len);
    }
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static size_t build_announce_frame(chat_ctx_t *c, const char *nickname,
                                   uint8_t ttl, uint8_t *out, size_t cap) {
    /* AnnouncementPacket TLV: nickname(0x01), noise_pk(0x02), signing_pk(0x03) */
    uint8_t tlv[256];
    size_t nicklen = strlen(nickname);
    if (nicklen > 200) nicklen = 200;
    size_t off = 0;
    tlv[off++] = 0x01; tlv[off++] = (uint8_t)nicklen;
    memcpy(tlv + off, nickname, nicklen); off += nicklen;
    tlv[off++] = 0x02; tlv[off++] = 32;
    memcpy(tlv + off, c->id.noise_pk, 32); off += 32;
    tlv[off++] = 0x03; tlv[off++] = 32;
    memcpy(tlv + off, c->id.signing_pk, 32); off += 32;

    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_ANNOUNCE, .ttl = ttl,
        .timestamp = now_ms(),
        .sender_id = c->id.peer_id,
        .payload = tlv, .payload_len = off,
    };
    return bc_enc_encode_and_sign(&p, (const struct bc_identity *)&c->id, out, cap);
}

static size_t build_message_frame(chat_ctx_t *c, const char *text,
                                  uint8_t ttl, uint8_t *out, size_t cap) {
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = ttl,
        .timestamp = now_ms(),
        .sender_id = c->id.peer_id,
        .payload = (const uint8_t *)text, .payload_len = strlen(text),
    };
    return bc_enc_encode_and_sign(&p, (const struct bc_identity *)&c->id, out, cap);
}

/* Inbound frame handler — decode, dedup, display, relay. */
static void on_frame_chat(const uint8_t *data, size_t len,
                          const char *peer_path, void *user) {
    chat_ctx_t *c = (chat_ctx_t *)user;
    bc_packet_t pkt;
    if (bc_packet_decode(data, len, &pkt) != BC_OK) return;

    /* Drop our own echoes. */
    if (memcmp(pkt.sender_id, c->id.peer_id, 8) == 0) {
        bc_packet_free(&pkt);
        return;
    }

    /* Dedup on (sender, timestamp, type). */
    if (bc_dedup_seen_or_add(c->dedup, pkt.sender_id, pkt.timestamp,
                             pkt.type, now_ms())) {
        bc_packet_free(&pkt);
        return;
    }

    char sender_hex[17];
    static const char tab[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        sender_hex[i * 2]     = tab[(pkt.sender_id[i] >> 4) & 0xf];
        sender_hex[i * 2 + 1] = tab[pkt.sender_id[i] & 0xf];
    }
    sender_hex[16] = '\0';

    switch (pkt.type) {
    case BC_MSG_ANNOUNCE: {
        bc_announce_t ann;
        if (bc_announce_decode(pkt.payload, pkt.payload_len, &ann)) {
            printf("[announce] %s (%s)\n", ann.nickname, sender_hex);
            fflush(stdout);
        }
        break;
    }
    case BC_MSG_MESSAGE: {
        printf("<%s> %.*s\n", sender_hex,
               (int)pkt.payload_len, pkt.payload);
        fflush(stdout);
        break;
    }
    case BC_MSG_LEAVE:
        printf("[leave] %s\n", sender_hex);
        fflush(stdout);
        break;
    default:
        fprintf(stderr, "[frame] %s type=0x%02x len=%zu from=%s\n",
                bc_msg_type_name(pkt.type), pkt.type, len,
                peer_path ? peer_path : "?");
        break;
    }

    /* Relay path: unencrypted public messages only. TTL decrement. */
    if (c->relay && pkt.ttl > 1
        && (pkt.type == BC_MSG_MESSAGE
            || pkt.type == BC_MSG_ANNOUNCE
            || pkt.type == BC_MSG_LEAVE)
        && !pkt.has_recipient) {
        /* Rebuild the frame with ttl-1. Cheapest path: mutate ttl byte in
         * a copy of the original frame — ttl is excluded from the signing
         * input per BitchatPacket.toBinaryDataForSigning, so the existing
         * signature stays valid. */
        uint8_t *copy = (uint8_t *)malloc(len);
        if (copy) {
            memcpy(copy, data, len);
            /* ttl lives at offset 2 regardless of version. */
            copy[2] = (uint8_t)(pkt.ttl - 1);
            bc_ble_broadcast(c->ble, copy, len);
            free(copy);
        }
    }

    bc_packet_free(&pkt);
}

static volatile int g_should_exit = 0;
static bc_ble_ctx_t *g_ble_for_signal = NULL;
static void sigint_handler(int s) {
    (void)s;
    g_should_exit = 1;
    if (g_ble_for_signal) bc_ble_stop(g_ble_for_signal);
}

/* Called by ble.c when a central subscribes (or anything else happens to
 * a peer path). On a new subscription we schedule a delayed replay of
 * the recent send queue: BlueZ needs ~200ms after StartNotify before
 * it's fully wired to forward our notifications over the air. Emitting
 * immediately would race the subscription setup and BlueZ would drop
 * the frames before the BLE stack acknowledged. */
#define BC_REPLAY_DELAY_MS 300

static void on_peer_chat(const char *peer_path, const char *event, void *user) {
    chat_ctx_t *c = (chat_ctx_t *)user;
    (void)peer_path;
    if (!c) return;
    if (strcmp(event, "start-notify") == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        c->replay_at_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000
                        + BC_REPLAY_DELAY_MS;
        fprintf(stderr, "[chat] subscriber attached → replay in %d ms\n",
                BC_REPLAY_DELAY_MS);
    }
}

static int cmd_chat(int use_testnet, const char *adapter,
                    const char *nickname, int enable_relay) {
    memset(&g_chat, 0, sizeof(g_chat));
    g_chat.use_testnet = use_testnet;
    g_chat.relay = enable_relay;
    g_chat.nickname = nickname;
    sent_queue_head = 0;
    sent_queue_count = 0;

    if (bc_identity_load_or_generate(NULL, &g_chat.id) != 0) {
        fprintf(stderr, "identity init failed\n");
        return 1;
    }
    char id_hex[17];
    bc_identity_peer_id_hex(&g_chat.id, id_hex);
    fprintf(stderr, "[chat] peer_id=%s  nickname=\"%s\"  relay=%s  net=%s\n",
            id_hex, nickname, enable_relay ? "on" : "off",
            use_testnet ? "test" : "main");

    g_chat.dedup = bc_dedup_new();
    if (!g_chat.dedup) { fprintf(stderr, "dedup alloc\n"); return 1; }

    g_chat.ble = bc_ble_new(on_frame_chat, on_peer_chat, &g_chat);
    if (!g_chat.ble) { fprintf(stderr, "ble_new\n"); return 1; }
    g_ble_for_signal = g_chat.ble;
    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    if (bc_ble_start(g_chat.ble, adapter, use_testnet) != 0) {
        fprintf(stderr, "ble_start failed\n");
        return 1;
    }
    if (bc_ble_enable_peripheral(g_chat.ble, nickname, use_testnet) != 0) {
        fprintf(stderr, "peripheral enable failed\n");
        return 1;
    }

    /* Announce immediately, then every 5s. The short interval covers the
     * gap between "peripheral advertises" and "remote central subscribes";
     * a subscriber that joins mid-interval will also trigger a send-queue
     * replay via on_peer_chat. */
    uint8_t ann[1024];
    size_t alen = build_announce_frame(&g_chat, nickname, 7, ann, sizeof(ann));
    if (alen > 0) {
        sent_queue_push(ann, alen);
        bc_ble_broadcast(g_chat.ble, ann, alen);
        bc_ble_central_write(g_chat.ble, ann, alen);
    }
    uint64_t last_announce = now_ms();

    int ble_fd = bc_ble_get_fd(g_chat.ble);
    char line[512];

    fprintf(stderr, "[chat] type messages, Ctrl-D or Ctrl-C to exit.\n");

    while (!g_should_exit) {
        struct pollfd pfds[2];
        pfds[0].fd = ble_fd;    pfds[0].events = POLLIN;
        pfds[1].fd = STDIN_FILENO; pfds[1].events = POLLIN;
        int timeout_ms = 100;
        int pr = poll(pfds, 2, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll"); break;
        }
        if (pfds[0].revents & POLLIN) bc_ble_process(g_chat.ble);

        /* Drive the BLE Connect scheduler every tick so jittered
         * reconnects actually fire. */
        bc_ble_tick(g_chat.ble);

        /* Fire any pending post-subscribe replay once BlueZ has wired up
         * its forward path. on_peer_chat arms this on start-notify; the
         * delay lets BlueZ finish the StartNotify → BLE-forward handshake
         * before we push frames into the characteristic. */
        if (g_chat.replay_at_ms && now_ms() >= g_chat.replay_at_ms) {
            fprintf(stderr, "[chat] replaying send queue (%d frames)\n",
                    sent_queue_count);
            sent_queue_replay(g_chat.ble);
            g_chat.replay_at_ms = 0;
        }

        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            if (fgets(line, sizeof(line), stdin) == NULL) {
                g_should_exit = 1;
                break;
            }
            /* strip trailing newline */
            size_t ll = strlen(line);
            while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r')) line[--ll] = 0;
            if (ll == 0) continue;

            uint8_t frame[1024];
            size_t flen = build_message_frame(&g_chat, line, 7, frame, sizeof(frame));
            if (flen > 0) {
                sent_queue_push(frame, flen);
                /* Two send paths because in a dual-role mesh either side
                 * may be central or peripheral on a given connection.
                 * bc_ble_broadcast covers the peripheral→central direction
                 * via Notify; bc_ble_central_write covers the
                 * central→peripheral direction via WriteValue. Whichever
                 * side won the staggered Connect race uses one; the other
                 * uses the other. */
                bc_ble_broadcast(g_chat.ble, frame, flen);
                bc_ble_central_write(g_chat.ble, frame, flen);
                printf("<me> %s\n", line);
                fflush(stdout);
            } else {
                fprintf(stderr, "[chat] encode failed\n");
            }
        }

        /* Re-announce every 5s so newly-arrived peers see us quickly. */
        if (now_ms() - last_announce > 5000) {
            alen = build_announce_frame(&g_chat, nickname, 7, ann, sizeof(ann));
            if (alen > 0) {
                bc_ble_broadcast(g_chat.ble, ann, alen);
                bc_ble_central_write(g_chat.ble, ann, alen);
            }
            last_announce = now_ms();
        }
    }

    bc_ble_free(g_chat.ble);
    bc_dedup_free(g_chat.dedup);
    g_ble_for_signal = NULL;
    return 0;
}

static int cmd_announce(int use_testnet, const char *adapter, const char *nickname) {
    memset(&g_chat, 0, sizeof(g_chat));
    g_chat.use_testnet = use_testnet;

    if (bc_identity_load_or_generate(NULL, &g_chat.id) != 0) {
        fprintf(stderr, "identity init failed\n");
        return 1;
    }
    char id_hex[17];
    bc_identity_peer_id_hex(&g_chat.id, id_hex);
    fprintf(stderr, "[announce] peer_id=%s  nickname=\"%s\"\n", id_hex, nickname);

    g_chat.ble = bc_ble_new(NULL, NULL, &g_chat);
    if (!g_chat.ble) { fprintf(stderr, "ble_new\n"); return 1; }
    if (bc_ble_start(g_chat.ble, adapter, use_testnet) != 0) return 1;
    if (bc_ble_enable_peripheral(g_chat.ble, nickname, use_testnet) != 0) return 1;

    uint8_t frame[1024];
    size_t flen = build_announce_frame(&g_chat, nickname, 7, frame, sizeof(frame));
    if (flen == 0) { fprintf(stderr, "[announce] encode failed\n"); return 1; }
    bc_ble_broadcast(g_chat.ble, frame, flen);

    /* Keep alive 10s so subscribers can read. */
    int ble_fd = bc_ble_get_fd(g_chat.ble);
    uint64_t deadline = now_ms() + 10000;
    while (now_ms() < deadline) {
        struct pollfd p = { .fd = ble_fd, .events = POLLIN };
        poll(&p, 1, 200);
        bc_ble_process(g_chat.ble);
    }
    bc_ble_free(g_chat.ble);
    return 0;
}

static void usage(void) {
    fputs(
        "bitchat-linux — C client for the BitChat mesh\n"
        "Usage:\n"
        "  bitchat-linux --decode                       Decode hex BitchatPacket on stdin\n"
        "  bitchat-linux --listen [--adapter <path>]    Join mainnet mesh via BLE (receive-only)\n"
        "  bitchat-linux --listen-test [--adapter ...]  Listen on testnet service UUID\n"
        "  bitchat-linux --listen-stream                Read length-prefixed frames on stdin\n"
        "                                               (software mock for CI / no-BLE boxes)\n"
        "  bitchat-linux --announce --nick <name>       Send one signed announce and exit\n"
        "  bitchat-linux --chat --nick <name>           Full dual-role mesh citizen:\n"
        "                                               scan+subscribe, advertise+serve GATT,\n"
        "                                               stdin → signed public messages,\n"
        "                                               inbound → displayed + relayed\n"
        "  bitchat-linux --self-test                    Run built-in round-trip tests\n"
        "  bitchat-linux --help                         Show this help\n"
        "\n"
        "  --adapter <path>   e.g. /org/bluez/hci1 — pick a specific BlueZ adapter\n"
        "  --testnet          Use the testnet service UUID (for --announce / --chat)\n"
        "  --no-relay         Disable the relay path in --chat (default: on)\n"
        "\n"
        "Identity is generated on first run and persisted at\n"
        "  $XDG_CONFIG_HOME/bitchat-linux/identity.bin  (0600)\n",
        stderr);
}

/* Scan argv for --adapter / --nick / --testnet / --no-relay anywhere
 * after the subcommand token. Simple since flag-order-independent. */
static const char *find_flag_value(int argc, char **argv, const char *flag) {
    for (int i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return NULL;
}
static int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 2; i < argc; i++) if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    const char *sub = argv[1];

    if (strcmp(sub, "--decode") == 0)        return cmd_decode();
    if (strcmp(sub, "--listen") == 0)        return cmd_listen(0, find_flag_value(argc, argv, "--adapter"));
    if (strcmp(sub, "--listen-test") == 0)   return cmd_listen(1, find_flag_value(argc, argv, "--adapter"));
    if (strcmp(sub, "--listen-stream") == 0) return cmd_listen_stream();
    if (strcmp(sub, "--self-test") == 0)     return self_test();

    if (strcmp(sub, "--announce") == 0 || strcmp(sub, "--chat") == 0) {
        const char *nick = find_flag_value(argc, argv, "--nick");
        if (!nick) nick = getenv("USER");
        if (!nick) nick = "linux";
        const char *adapter = find_flag_value(argc, argv, "--adapter");
        int testnet = has_flag(argc, argv, "--testnet");
        if (strcmp(sub, "--announce") == 0) {
            return cmd_announce(testnet, adapter, nick);
        }
        int relay = !has_flag(argc, argv, "--no-relay");
        return cmd_chat(testnet, adapter, nick, relay);
    }

    if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0) {
        usage();
        return 0;
    }
    usage();
    return 2;
}
