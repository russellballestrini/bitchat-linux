/*
 * Persistent identity: Ed25519 signing key + Curve25519 (X25519) noise key.
 *
 * On-disk format (at ~/.config/bitchat-linux/identity.bin, mode 0600):
 *   magic(4) = "BCI1"
 *   version(1) = 0x01
 *   ed25519_seed(32)     — Ed25519 private key seed
 *   ed25519_pubkey(32)
 *   x25519_privkey(32)
 *   x25519_pubkey(32)
 *
 * Peer ID is derived lazily: SHA256(x25519_pubkey)[0..8] — matches
 * BLEService.swift:2894 (refreshPeerIdentity).
 *
 * This is free and unencumbered software released into the public domain.
 */

#ifndef BITCHAT_IDENTITY_H
#define BITCHAT_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#define BC_ID_SIGNING_PK   32
#define BC_ID_SIGNING_SEED 32
#define BC_ID_NOISE_PK     32
#define BC_ID_NOISE_SK     32
#define BC_ID_PEER_ID       8
#define BC_ID_SIGNATURE    64

typedef struct {
    uint8_t signing_seed[BC_ID_SIGNING_SEED];   /* Ed25519 seed (private) */
    uint8_t signing_pk[BC_ID_SIGNING_PK];       /* Ed25519 public */
    uint8_t noise_sk[BC_ID_NOISE_SK];           /* X25519 private */
    uint8_t noise_pk[BC_ID_NOISE_PK];           /* X25519 public */
    uint8_t peer_id[BC_ID_PEER_ID];             /* SHA256(noise_pk)[0..8] */
} bc_identity_t;

/* Load identity from `path`, or generate and save if missing. Returns 0 on
 * success. If path is NULL, uses $XDG_CONFIG_HOME/bitchat-linux/identity.bin
 * (or ~/.config/bitchat-linux/identity.bin). */
int bc_identity_load_or_generate(const char *path, bc_identity_t *out);

/* Sign `msg` of length `mlen` with the Ed25519 signing seed. Writes 64
 * bytes into sig_out. Returns 0 on success. */
int bc_identity_sign(const bc_identity_t *id,
                     const uint8_t *msg, size_t mlen,
                     uint8_t sig_out[BC_ID_SIGNATURE]);

/* Verify a 64-byte Ed25519 signature over `msg` against `pubkey` (32
 * bytes). Returns 1 on valid, 0 on invalid, <0 on error. */
int bc_identity_verify(const uint8_t pubkey[BC_ID_SIGNING_PK],
                       const uint8_t sig[BC_ID_SIGNATURE],
                       const uint8_t *msg, size_t mlen);

/* Hex-encode the peer ID into buf (17 bytes = 16 hex + NUL). */
void bc_identity_peer_id_hex(const bc_identity_t *id, char buf[17]);

#endif /* BITCHAT_IDENTITY_H */
