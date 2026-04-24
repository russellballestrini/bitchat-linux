#include "harness.h"
#include "announce.h"
#include "fixtures.h"

#include <string.h>

TEST(decode_required_fields) {
    uint8_t tlv[128];
    size_t n = fx_build_announce_tlv("fox", tlv, sizeof(tlv));
    CHECK(n > 0);

    bc_announce_t ann;
    CHECK(bc_announce_decode(tlv, n, &ann));
    CHECK_EQ_STR("fox", ann.nickname);
    CHECK_EQ_INT(3, (int)ann.nickname_len);
    CHECK(ann.has_noise_pubkey);
    CHECK(ann.has_signing_pubkey);
    CHECK_EQ_MEM(FX_NOISE_PUBKEY,   ann.noise_pubkey,   32);
    CHECK_EQ_MEM(FX_SIGNING_PUBKEY, ann.signing_pubkey, 32);
    CHECK_EQ_INT(0, (int)ann.neighbor_count);
}

TEST(decode_with_neighbors) {
    const uint8_t neighbors[] = {
        0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
        0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,
    };
    uint8_t tlv[128];
    size_t n = fx_build_announce_tlv_with_neighbors("bob", neighbors, 2, tlv, sizeof(tlv));
    CHECK(n > 0);

    bc_announce_t ann;
    CHECK(bc_announce_decode(tlv, n, &ann));
    CHECK_EQ_STR("bob", ann.nickname);
    CHECK_EQ_INT(2, (int)ann.neighbor_count);
    CHECK_EQ_MEM(neighbors,     ann.neighbors[0], 8);
    CHECK_EQ_MEM(neighbors + 8, ann.neighbors[1], 8);
}

TEST(rejects_without_nickname) {
    /* Only pubkeys, no TLV 0x01. */
    uint8_t tlv[128];
    size_t off = 0;
    tlv[off++] = 0x02; tlv[off++] = 32;
    memcpy(tlv + off, FX_NOISE_PUBKEY, 32); off += 32;
    tlv[off++] = 0x03; tlv[off++] = 32;
    memcpy(tlv + off, FX_SIGNING_PUBKEY, 32); off += 32;

    bc_announce_t ann;
    CHECK(!bc_announce_decode(tlv, off, &ann));
}

TEST(rejects_without_noise_key) {
    uint8_t tlv[128];
    size_t off = 0;
    tlv[off++] = 0x01; tlv[off++] = 3;
    memcpy(tlv + off, "fox", 3); off += 3;
    tlv[off++] = 0x03; tlv[off++] = 32;
    memcpy(tlv + off, FX_SIGNING_PUBKEY, 32); off += 32;

    bc_announce_t ann;
    CHECK(!bc_announce_decode(tlv, off, &ann));
}

TEST(unknown_tlv_types_ignored) {
    uint8_t tlv[160];
    size_t base = fx_build_announce_tlv("fox", tlv, sizeof(tlv));
    /* Append TLV type 0x77 (unknown) — decoder should skip it */
    tlv[base++] = 0x77; tlv[base++] = 4;
    tlv[base++] = 0xaa; tlv[base++] = 0xbb; tlv[base++] = 0xcc; tlv[base++] = 0xdd;

    bc_announce_t ann;
    CHECK(bc_announce_decode(tlv, base, &ann));
    CHECK_EQ_STR("fox", ann.nickname);
}

int main(void) {
    RUN_TESTS(decode_required_fields,
              decode_with_neighbors,
              rejects_without_nickname,
              rejects_without_noise_key,
              unknown_tlv_types_ignored);
}
