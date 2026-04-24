/*
 * Hex string <-> bytes helpers.
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_HEX_H
#define BITCHAT_HEX_H

#include <stddef.h>
#include <stdint.h>

/* Parse hex string (may include whitespace + 0x prefix) into out. Returns
 * number of bytes written, or -1 on invalid input or capacity overflow. */
long bc_hex_decode(const char *str, uint8_t *out, size_t cap);

/* Encode bytes into a lowercase hex string (not null-terminated separator
 * between bytes). out must have capacity >= 2*len + 1. */
void bc_hex_encode(const uint8_t *in, size_t len, char *out);

#endif /* BITCHAT_HEX_H */
