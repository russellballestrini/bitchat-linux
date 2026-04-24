#include "harness.h"
#include "hex.h"

#include <string.h>

TEST(decode_lowercase) {
    uint8_t out[8];
    long n = bc_hex_decode("deadbeef", out, sizeof(out));
    CHECK_EQ_INT(4, n);
    uint8_t want[] = {0xde, 0xad, 0xbe, 0xef};
    CHECK_EQ_MEM(want, out, 4);
}

TEST(decode_mixed_case_with_whitespace) {
    uint8_t out[8];
    long n = bc_hex_decode("De Ad BE ef\n 00 01", out, sizeof(out));
    CHECK_EQ_INT(6, n);
    uint8_t want[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};
    CHECK_EQ_MEM(want, out, 6);
}

TEST(decode_0x_prefix) {
    uint8_t out[4];
    long n = bc_hex_decode("0xCAFEBABE", out, sizeof(out));
    CHECK_EQ_INT(4, n);
    uint8_t want[] = {0xca, 0xfe, 0xba, 0xbe};
    CHECK_EQ_MEM(want, out, 4);
}

TEST(decode_reject_odd_nibble_count) {
    uint8_t out[4];
    CHECK_EQ_INT(-1, bc_hex_decode("abc", out, sizeof(out)));
}

TEST(decode_reject_non_hex) {
    uint8_t out[4];
    CHECK_EQ_INT(-1, bc_hex_decode("deadzz", out, sizeof(out)));
}

TEST(decode_reject_overflow) {
    uint8_t out[2];
    CHECK_EQ_INT(-1, bc_hex_decode("01020304", out, sizeof(out)));
}

TEST(encode_roundtrip) {
    const uint8_t in[] = {0x01, 0xff, 0xab};
    char out[8];
    bc_hex_encode(in, sizeof(in), out);
    CHECK_EQ_STR("01ffab", out);
}

int main(void) {
    RUN_TESTS(decode_lowercase,
              decode_mixed_case_with_whitespace,
              decode_0x_prefix,
              decode_reject_odd_nibble_count,
              decode_reject_non_hex,
              decode_reject_overflow,
              encode_roundtrip);
}
