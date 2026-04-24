/*
 * Message-ID dedup cache for the relay path.
 *
 * Each public packet is identified by the triple (senderID[8], timestamp,
 * type). Relay-side logic looks this up on every inbound packet; on a
 * miss, the packet is rebroadcast (with TTL decremented). On a hit, it's
 * dropped to avoid amplification loops.
 *
 * Entries expire after BC_DEDUP_TTL_MS milliseconds.
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_DEDUP_H
#define BITCHAT_DEDUP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BC_DEDUP_TTL_MS  (5 * 60 * 1000)   /* 5 minutes */
#define BC_DEDUP_SLOTS   512                /* small, fixed size */

typedef struct bc_dedup bc_dedup_t;

bc_dedup_t *bc_dedup_new(void);
void        bc_dedup_free(bc_dedup_t *d);

/* Returns true if the triple was already in the cache. Adds it either way
 * and evicts oldest entries when full. now_ms is injected for testability. */
bool bc_dedup_seen_or_add(bc_dedup_t *d,
                          const uint8_t sender_id[8],
                          uint64_t timestamp,
                          uint8_t type,
                          uint64_t now_ms);

#endif /* BITCHAT_DEDUP_H */
