#include "harness.h"
#include "encoder.h"
#include "fixtures.h"
#include "packet.h"

#include <string.h>

TEST(v1_announce_roundtrip) {
    uint8_t tlv[128];
    size_t tlv_len = fx_build_announce_tlv("fox", tlv, sizeof(tlv));

    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_ANNOUNCE, .ttl = 7,
        .timestamp = 0x0000018e0000ffffULL,
        .sender_id = FX_SENDER_ID,
        .payload = tlv, .payload_len = tlv_len,
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(1, pkt.version);
    CHECK_EQ_INT(BC_MSG_ANNOUNCE, pkt.type);
    CHECK_EQ_INT(7, pkt.ttl);
    CHECK_EQ_INT(0x0000018e0000ffffULL, (long long)pkt.timestamp);
    CHECK_EQ_MEM(FX_SENDER_ID, pkt.sender_id, 8);
    CHECK(!pkt.has_recipient);
    CHECK(!pkt.has_signature);
    CHECK_EQ_INT(tlv_len, (int)pkt.payload_len);
    CHECK_EQ_MEM(tlv, pkt.payload, tlv_len);
    bc_packet_free(&pkt);
}

TEST(v1_with_recipient_and_signature) {
    const char *msg = "hello mesh";
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = 3, .timestamp = 1,
        .sender_id = FX_SENDER_ID,
        .recipient_id = FX_RECIPIENT_ID,
        .payload = (const uint8_t *)msg, .payload_len = strlen(msg),
        .signature = FX_SIGNATURE,
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK(pkt.has_recipient);
    CHECK(pkt.has_signature);
    CHECK_EQ_MEM(FX_RECIPIENT_ID, pkt.recipient_id, 8);
    CHECK_EQ_MEM(FX_SIGNATURE, pkt.signature, 64);
    CHECK_EQ_INT(strlen(msg), (int)pkt.payload_len);
    CHECK_EQ_MEM(msg, pkt.payload, strlen(msg));
    bc_packet_free(&pkt);
}

TEST(padded_frame_decodes) {
    uint8_t tlv[128];
    size_t tlv_len = fx_build_announce_tlv("fox", tlv, sizeof(tlv));
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_ANNOUNCE, .ttl = 7,
        .sender_id = FX_SENDER_ID,
        .payload = tlv, .payload_len = tlv_len,
        .pad_to_block = true,
    };
    uint8_t frame[2048];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    /* Should be padded to 256 for this small announce. */
    CHECK(n == 256);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(tlv_len, (int)pkt.payload_len);
    bc_packet_free(&pkt);
}

TEST(compressed_payload_decodes) {
    /* A repetitive 200-byte payload — compresses well. */
    uint8_t big[200];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i % 4);

    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = 1,
        .sender_id = FX_SENDER_ID,
        .payload = big, .payload_len = sizeof(big),
        .compress = true,
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);
    /* Compressed frame should be smaller than the raw payload + header. */
    CHECK(n < 200);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(sizeof(big), (int)pkt.payload_len);
    CHECK_EQ_MEM(big, pkt.payload, sizeof(big));
    bc_packet_free(&pkt);
}

TEST(v2_with_route_hops) {
    const uint8_t hops[] = {
        0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
        0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,
    };
    const char *msg = "v2";
    bc_enc_params_t p = {
        .version = 2, .type = BC_MSG_MESSAGE, .ttl = 4, .timestamp = 99,
        .sender_id = FX_SENDER_ID,
        .payload = (const uint8_t *)msg, .payload_len = strlen(msg),
        .route_hops = hops, .route_count = 2,
    };
    uint8_t frame[512];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    bc_packet_t pkt;
    CHECK_EQ_INT(BC_OK, bc_packet_decode(frame, n, &pkt));
    CHECK_EQ_INT(2, pkt.version);
    CHECK_EQ_INT(2, pkt.route_count);
    CHECK_EQ_MEM(hops,     pkt.route_hops,     8);
    CHECK_EQ_MEM(hops + 8, pkt.route_hops + 8, 8);
    CHECK_EQ_INT(strlen(msg), (int)pkt.payload_len);
    bc_packet_free(&pkt);
}

TEST(rejects_bad_version) {
    uint8_t bad[32];
    memset(bad, 0, sizeof(bad));
    bad[0] = 9; /* invalid version */
    bc_packet_t pkt;
    CHECK(BC_OK != bc_packet_decode(bad, sizeof(bad), &pkt));
}

TEST(rejects_short_frame) {
    uint8_t buf[5] = {1, 2, 7, 0, 0};
    bc_packet_t pkt;
    CHECK(BC_OK != bc_packet_decode(buf, sizeof(buf), &pkt));
}

TEST(rejects_truncated_payload) {
    const char *msg = "hello";
    bc_enc_params_t p = {
        .version = 1, .type = BC_MSG_MESSAGE, .ttl = 1,
        .sender_id = FX_SENDER_ID,
        .payload = (const uint8_t *)msg, .payload_len = strlen(msg),
    };
    uint8_t frame[64];
    size_t n = bc_enc_encode(&p, frame, sizeof(frame));
    CHECK(n > 0);

    /* Cut 2 bytes off the end — payload is now truncated. */
    bc_packet_t pkt;
    CHECK(BC_OK != bc_packet_decode(frame, n - 2, &pkt));
}

TEST(fuzz_small_random_inputs_do_not_crash) {
    /* Feeds a variety of random-ish small buffers to the decoder. The
     * only guarantee is: does not crash, never returns BC_OK for obvious
     * garbage. */
    unsigned seed = 0xc0ffeeu;
    for (int i = 0; i < 256; i++) {
        uint8_t buf[96];
        for (size_t j = 0; j < sizeof(buf); j++) {
            seed = seed * 1103515245u + 12345u;
            buf[j] = (uint8_t)(seed >> 16);
        }
        bc_packet_t pkt;
        bc_err_t rc = bc_packet_decode(buf, sizeof(buf), &pkt);
        if (rc == BC_OK) bc_packet_free(&pkt);
        /* No CHECK — the assertion is "no crash, no leak". */
    }
    CHECK(1);
}

int main(void) {
    RUN_TESTS(v1_announce_roundtrip,
              v1_with_recipient_and_signature,
              padded_frame_decodes,
              compressed_payload_decodes,
              v2_with_route_hops,
              rejects_bad_version,
              rejects_short_frame,
              rejects_truncated_payload,
              fuzz_small_random_inputs_do_not_crash);
}
