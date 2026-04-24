#include "dedup.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t added_ms;
    uint64_t timestamp;
    uint8_t  sender_id[8];
    uint8_t  type;
    uint8_t  used;
} bc_dedup_entry_t;

struct bc_dedup {
    bc_dedup_entry_t slots[BC_DEDUP_SLOTS];
};

bc_dedup_t *bc_dedup_new(void) {
    bc_dedup_t *d = (bc_dedup_t *)calloc(1, sizeof(*d));
    return d;
}

void bc_dedup_free(bc_dedup_t *d) { free(d); }

static int entries_match(const bc_dedup_entry_t *e,
                         const uint8_t sender_id[8],
                         uint64_t timestamp, uint8_t type) {
    return e->type == type
        && e->timestamp == timestamp
        && memcmp(e->sender_id, sender_id, 8) == 0;
}

bool bc_dedup_seen_or_add(bc_dedup_t *d,
                          const uint8_t sender_id[8],
                          uint64_t timestamp,
                          uint8_t type,
                          uint64_t now_ms) {
    int empty_slot = -1;
    int oldest_slot = 0;
    uint64_t oldest_ms = UINT64_MAX;

    for (int i = 0; i < BC_DEDUP_SLOTS; i++) {
        bc_dedup_entry_t *e = &d->slots[i];
        if (!e->used) { if (empty_slot < 0) empty_slot = i; continue; }
        if (now_ms > e->added_ms + BC_DEDUP_TTL_MS) {
            /* Expired — free the slot (and treat as never-seen). */
            e->used = 0;
            if (empty_slot < 0) empty_slot = i;
            continue;
        }
        if (entries_match(e, sender_id, timestamp, type)) return true;
        if (e->added_ms < oldest_ms) { oldest_ms = e->added_ms; oldest_slot = i; }
    }

    int slot = (empty_slot >= 0) ? empty_slot : oldest_slot;
    bc_dedup_entry_t *e = &d->slots[slot];
    e->used = 1;
    e->added_ms = now_ms;
    e->timestamp = timestamp;
    e->type = type;
    memcpy(e->sender_id, sender_id, 8);
    return false;
}
