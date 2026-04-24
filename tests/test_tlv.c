#include "harness.h"
#include "tlv.h"

#include <string.h>

TEST(empty) {
    bc_tlv_iter_t it;
    bc_tlv_iter_init(&it, NULL, 0);
    bc_tlv_t tlv;
    CHECK(!bc_tlv_next(&it, &tlv));
}

TEST(single) {
    const uint8_t buf[] = {0x01, 0x03, 'f', 'o', 'x'};
    bc_tlv_iter_t it;
    bc_tlv_iter_init(&it, buf, sizeof(buf));
    bc_tlv_t tlv;
    CHECK(bc_tlv_next(&it, &tlv));
    CHECK_EQ_INT(0x01, tlv.type);
    CHECK_EQ_INT(3, tlv.length);
    CHECK_EQ_MEM("fox", tlv.value, 3);
    CHECK(!bc_tlv_next(&it, &tlv));
}

TEST(multiple) {
    const uint8_t buf[] = {
        0x01, 0x02, 'h', 'i',
        0x02, 0x01, 0x42,
        0x07, 0x00,
    };
    bc_tlv_iter_t it;
    bc_tlv_iter_init(&it, buf, sizeof(buf));
    bc_tlv_t t;
    CHECK(bc_tlv_next(&it, &t)); CHECK_EQ_INT(0x01, t.type); CHECK_EQ_INT(2, t.length);
    CHECK(bc_tlv_next(&it, &t)); CHECK_EQ_INT(0x02, t.type); CHECK_EQ_INT(1, t.length);
    CHECK(bc_tlv_next(&it, &t)); CHECK_EQ_INT(0x07, t.type); CHECK_EQ_INT(0, t.length);
    CHECK(!bc_tlv_next(&it, &t));
}

TEST(rejects_truncated_value) {
    /* Header says length 10 but only 3 bytes of value follow. */
    const uint8_t buf[] = {0x01, 0x0a, 'a', 'b', 'c'};
    bc_tlv_iter_t it;
    bc_tlv_iter_init(&it, buf, sizeof(buf));
    bc_tlv_t t;
    CHECK(!bc_tlv_next(&it, &t));
}

TEST(rejects_truncated_header) {
    const uint8_t buf[] = {0x01};
    bc_tlv_iter_t it;
    bc_tlv_iter_init(&it, buf, sizeof(buf));
    bc_tlv_t t;
    CHECK(!bc_tlv_next(&it, &t));
}

int main(void) {
    RUN_TESTS(empty, single, multiple,
              rejects_truncated_value, rejects_truncated_header);
}
