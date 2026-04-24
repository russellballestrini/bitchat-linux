#include "harness.h"
#include "packet.h"

#include <string.h>

TEST(unpad_noop_on_zero_byte_trailer) {
    const uint8_t d[] = {0x01, 0x02, 0x00};
    /* last byte is 0 — not valid PKCS#7, leave as-is */
    CHECK_EQ_INT(3, bc_unpad(d, sizeof(d)));
}

TEST(unpad_strips_pkcs7) {
    const uint8_t d[] = {0x01, 0x02, 0x03, 0x03, 0x03};
    CHECK_EQ_INT(2, bc_unpad(d, sizeof(d)));
}

TEST(unpad_leaves_inconsistent_trailer) {
    /* Last byte says pad=3 but only 2 of the trailing 3 are 0x03 */
    const uint8_t d[] = {0x04, 0x05, 0x03, 0x03};
    CHECK_EQ_INT(4, bc_unpad(d, sizeof(d)));
}

TEST(unpad_strips_full_block) {
    uint8_t d[16];
    memset(d, 0x08, 8);    /* 8 meaningful bytes would be... */
    memset(d + 8, 8, 8);   /* followed by pad of length 8 */
    /* But the first 8 bytes are also 0x08 — trailer pad = 8 matches fine;
     * decoder still strips exactly 8 trailing bytes by last-byte length. */
    CHECK_EQ_INT(8, bc_unpad(d, 16));
}

TEST(unpad_ignores_pad_larger_than_len) {
    /* last byte 0x05 but total len < 5 */
    const uint8_t d[] = {0x05, 0x05};
    CHECK_EQ_INT(2, bc_unpad(d, sizeof(d)));
}

TEST(unpad_empty) {
    CHECK_EQ_INT(0, bc_unpad((const uint8_t *)"", 0));
}

int main(void) {
    RUN_TESTS(unpad_noop_on_zero_byte_trailer,
              unpad_strips_pkcs7,
              unpad_leaves_inconsistent_trailer,
              unpad_strips_full_block,
              unpad_ignores_pad_larger_than_len,
              unpad_empty);
}
