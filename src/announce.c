#include "announce.h"
#include "tlv.h"

#include <string.h>

#define TLV_NICKNAME         0x01
#define TLV_NOISE_PUBKEY     0x02
#define TLV_SIGNING_PUBKEY   0x03
#define TLV_DIRECT_NEIGHBORS 0x04

bool bc_announce_decode(const uint8_t *buf, size_t len, bc_announce_t *out) {
    memset(out, 0, sizeof(*out));

    bool have_nickname = false;

    bc_tlv_iter_t it;
    bc_tlv_iter_init(&it, buf, len);
    bc_tlv_t tlv;
    while (bc_tlv_next(&it, &tlv)) {
        switch (tlv.type) {
        case TLV_NICKNAME:
            memcpy(out->nickname, tlv.value, tlv.length);
            out->nickname[tlv.length] = '\0';
            out->nickname_len = tlv.length;
            have_nickname = true;
            break;

        case TLV_NOISE_PUBKEY:
            if (tlv.length == BC_ANNOUNCE_PUBKEY_SIZE) {
                memcpy(out->noise_pubkey, tlv.value, tlv.length);
                out->has_noise_pubkey = true;
            }
            break;

        case TLV_SIGNING_PUBKEY:
            if (tlv.length == BC_ANNOUNCE_PUBKEY_SIZE) {
                memcpy(out->signing_pubkey, tlv.value, tlv.length);
                out->has_signing_pubkey = true;
            }
            break;

        case TLV_DIRECT_NEIGHBORS:
            if (tlv.length > 0 && (tlv.length % BC_PEER_ID_SIZE) == 0) {
                size_t count = tlv.length / BC_PEER_ID_SIZE;
                if (count > BC_ANNOUNCE_MAX_NEIGHBORS)
                    count = BC_ANNOUNCE_MAX_NEIGHBORS;
                for (size_t i = 0; i < count; i++) {
                    memcpy(out->neighbors[i],
                           tlv.value + i * BC_PEER_ID_SIZE,
                           BC_PEER_ID_SIZE);
                }
                out->neighbor_count = count;
            }
            break;

        default:
            /* Unknown TLV: skip for forward compatibility. */
            break;
        }
    }

    return have_nickname && out->has_noise_pubkey && out->has_signing_pubkey;
}
