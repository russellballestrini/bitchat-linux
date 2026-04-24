#include "hex.h"

#include <ctype.h>

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

long bc_hex_decode(const char *str, uint8_t *out, size_t cap) {
    size_t n = 0;
    int high = -1;

    /* Skip leading 0x / 0X. */
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;

    for (const char *p = str; *p; p++) {
        if (isspace((unsigned char)*p)) continue;
        int v = hex_nibble(*p);
        if (v < 0) return -1;
        if (high < 0) {
            high = v;
        } else {
            if (n >= cap) return -1;
            out[n++] = (uint8_t)((high << 4) | v);
            high = -1;
        }
    }
    if (high >= 0) return -1;   /* odd number of nibbles */
    return (long)n;
}

void bc_hex_encode(const uint8_t *in, size_t len, char *out) {
    static const char tab[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i]     = tab[in[i] >> 4];
        out[2 * i + 1] = tab[in[i] & 0x0f];
    }
    out[2 * len] = '\0';
}
