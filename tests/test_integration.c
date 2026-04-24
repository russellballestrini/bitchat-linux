/*
 * Integration tests — exercise multiple modules end-to-end:
 *   encoder -> packet decoder -> TLV + announce parser
 *
 * These simulate what happens when a real mesh frame arrives over BLE:
 * the receive path takes raw bytes and must yield a fully-parsed
 * BitchatMessage with all decorations (padding stripped, payload
 * decompressed, signatures, etc.).
 */

#include "harness.h"
#include "announce.h"
#include "encoder.h"
#include "fixtures.h"
#include "packet.h"

#include <string.h>

TEST(padded_compressed_announce_end_to_end) {
    /* A realistic-size announce — 3 neighbors, nickname "russell",
     * pubkeys from fixtures. Compress + pad. Decoder has to strip both
     * padding and decompress, then we parse the TLV. */
    uint8_t neighbors[24];
    for (int i = 0; i < 24; i++) neighbors[i] = (uint8_t)(0x30 + i);

    uint8_t tlv[256];
    size_t tlv_len = fx_build_announce_tlv_with_neighbors(
        "russell", neighbors, 3, tlv, sizeof(tlv));
    CHECK(tlv_len > 0);
    CHECK(tlv_len >= 100); /* ensures compression triggers */

    bc_enc_params_t p = {
        .version = 2, .type = BC_MSG_ANNOUNCE, .ttl = 6,
        .timestamp = 0x0000018effffffffULL,
        .sender_id = FX_SENDER_ID,
        .payload = tlv, .payload_len = tlv_len,
        .compress = true, .pad_to_block = true,
    };
    uint8_t frame[2048];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    /* Full decode. */
    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(2, pkt.version);
    CHECK_EQ_INT(BC_MSG_ANNOUNCE, pkt.type);
    CHECK_EQ_INT(tlv_len, (int)pkt.payload_len);
    CHECK_EQ_MEM(tlv, pkt.payload, tlv_len);

    /* Parse announce out of decompressed payload. */
    bc_announce_t ann;
    CHECK(bc_announce_decode(pkt.payload, pkt.payload_len, &ann));
    CHECK_EQ_STR("russell", ann.nickname);
    CHECK_EQ_INT(3, (int)ann.neighbor_count);
    CHECK_EQ_MEM(neighbors,      ann.neighbors[0], 8);
    CHECK_EQ_MEM(neighbors + 8,  ann.neighbors[1], 8);
    CHECK_EQ_MEM(neighbors + 16, ann.neighbors[2], 8);
    CHECK_EQ_MEM(FX_NOISE_PUBKEY,   ann.noise_pubkey,   32);
    CHECK_EQ_MEM(FX_SIGNING_PUBKEY, ann.signing_pubkey, 32);

    bc_packet_free(&pkt);
}

TEST(public_message_utf8_survives_roundtrip) {
    const char *msg = "hello from the mesh — with unicode ☯";
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = 5,
        .sender_id = FX_SENDER_ID,
        .payload = (const uint8_t *)msg, .payload_len = strlen(msg),
        .pad_to_block = true,
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(BC_MSG_MESSAGE, pkt.type);
    CHECK_EQ_INT(strlen(msg), (int)pkt.payload_len);
    CHECK_EQ_MEM(msg, pkt.payload, strlen(msg));
    bc_packet_free(&pkt);
}

TEST(noise_handshake_passes_through_unmodified) {
    /* Type 0x10 is an opaque Noise handshake blob — decoder must not
     * interpret it; just pass the bytes through. */
    uint8_t blob[72];
    for (size_t i = 0; i < sizeof(blob); i++) blob[i] = (uint8_t)(i ^ 0x5a);

    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_NOISE_HANDSHAKE, .ttl = 2,
        .sender_id = FX_SENDER_ID,
        .recipient_id = FX_RECIPIENT_ID,
        .payload = blob, .payload_len = sizeof(blob),
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(BC_MSG_NOISE_HANDSHAKE, pkt.type);
    CHECK(pkt.has_recipient);
    CHECK_EQ_INT(sizeof(blob), (int)pkt.payload_len);
    CHECK_EQ_MEM(blob, pkt.payload, sizeof(blob));
    bc_packet_free(&pkt);
}

TEST(relay_simulation_ttl_preserved) {
    /* Simulate a relay: receive a frame, decode, then re-serialize
     * mentally — we just verify TTL round-trips unchanged (decrement
     * logic lives at a higher layer in Stage 3). */
    const char *msg = "x";
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = 7, .timestamp = 42,
        .sender_id = FX_SENDER_ID,
        .payload = (const uint8_t *)msg, .payload_len = 1,
    };
    uint8_t frame[64];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(7, pkt.ttl);
    CHECK_EQ_INT(42, (long long)pkt.timestamp);
    bc_packet_free(&pkt);
}

int main(void) {
    RUN_TESTS(padded_compressed_announce_end_to_end,
              public_message_utf8_survives_roundtrip,
              noise_handshake_passes_through_unmodified,
              relay_simulation_ttl_preserved);
}
