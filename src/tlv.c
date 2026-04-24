#include "tlv.h"

void bc_tlv_iter_init(bc_tlv_iter_t *it, const uint8_t *buf, size_t len) {
    it->buf = buf;
    it->len = len;
    it->off = 0;
}

bool bc_tlv_next(bc_tlv_iter_t *it, bc_tlv_t *out) {
    if (it->off + 2 > it->len) return false;
    uint8_t type   = it->buf[it->off];
    uint8_t length = it->buf[it->off + 1];
    if (it->off + 2 + (size_t)length > it->len) return false;
    out->type   = type;
    out->length = length;
    out->value  = it->buf + it->off + 2;
    it->off += 2 + (size_t)length;
    return true;
}
