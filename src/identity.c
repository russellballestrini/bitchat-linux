#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

static const uint8_t MAGIC[4] = { 'B', 'C', 'I', '1' };
#define FILE_VERSION  0x01
#define FILE_SIZE (4 + 1 + BC_ID_SIGNING_SEED + BC_ID_SIGNING_PK + BC_ID_NOISE_SK + BC_ID_NOISE_PK)

static int ensure_parent_dir(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
                fprintf(stderr, "mkdir %s: %s\n", tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    return 0;
}

static int default_path(char *buf, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(buf, cap, "%s/bitchat-linux/identity.bin", xdg);
        return 0;
    }
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : NULL;
    }
    if (!home) return -1;
    snprintf(buf, cap, "%s/.config/bitchat-linux/identity.bin", home);
    return 0;
}

static int derive_peer_id(const uint8_t noise_pk[BC_ID_NOISE_PK],
                          uint8_t peer_id[BC_ID_PEER_ID]) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(noise_pk, BC_ID_NOISE_PK, digest);
    memcpy(peer_id, digest, BC_ID_PEER_ID);
    return 0;
}

static int gen_ed25519(uint8_t seed_out[32], uint8_t pk_out[32]) {
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!pctx) return -1;
    int rc = -1;
    if (EVP_PKEY_keygen_init(pctx) <= 0) goto done;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) goto done;
    size_t sk_len = 32, pk_len = 32;
    if (EVP_PKEY_get_raw_private_key(pkey, seed_out, &sk_len) <= 0) goto done;
    if (EVP_PKEY_get_raw_public_key(pkey,  pk_out,   &pk_len) <= 0) goto done;
    if (sk_len != 32 || pk_len != 32) goto done;
    rc = 0;
done:
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);
    return rc;
}

static int gen_x25519(uint8_t sk_out[32], uint8_t pk_out[32]) {
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!pctx) return -1;
    int rc = -1;
    if (EVP_PKEY_keygen_init(pctx) <= 0) goto done;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) goto done;
    size_t sk_len = 32, pk_len = 32;
    if (EVP_PKEY_get_raw_private_key(pkey, sk_out, &sk_len) <= 0) goto done;
    if (EVP_PKEY_get_raw_public_key(pkey,  pk_out, &pk_len) <= 0) goto done;
    if (sk_len != 32 || pk_len != 32) goto done;
    rc = 0;
done:
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(pctx);
    return rc;
}

static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *out_n) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, cap);
    close(fd);
    if (n < 0) return -1;
    *out_n = (size_t)n;
    return 0;
}

static int write_file(const char *path, const uint8_t *buf, size_t n) {
    if (ensure_parent_dir(path) < 0) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { fprintf(stderr, "open %s: %s\n", path, strerror(errno)); return -1; }
    ssize_t w = write(fd, buf, n);
    if (close(fd) != 0 || w != (ssize_t)n) return -1;
    /* Belt + suspenders — make sure it's 0600 even if umask interfered. */
    chmod(path, 0600);
    return 0;
}

int bc_identity_load_or_generate(const char *path, bc_identity_t *out) {
    char pbuf[512];
    if (!path) {
        if (default_path(pbuf, sizeof(pbuf)) < 0) return -1;
        path = pbuf;
    }

    uint8_t file[FILE_SIZE + 8];
    size_t got = 0;
    if (read_file(path, file, sizeof(file), &got) == 0 && got == FILE_SIZE) {
        if (memcmp(file, MAGIC, 4) == 0 && file[4] == FILE_VERSION) {
            size_t off = 5;
            memcpy(out->signing_seed, file + off, BC_ID_SIGNING_SEED); off += BC_ID_SIGNING_SEED;
            memcpy(out->signing_pk,   file + off, BC_ID_SIGNING_PK);   off += BC_ID_SIGNING_PK;
            memcpy(out->noise_sk,     file + off, BC_ID_NOISE_SK);     off += BC_ID_NOISE_SK;
            memcpy(out->noise_pk,     file + off, BC_ID_NOISE_PK);
            derive_peer_id(out->noise_pk, out->peer_id);
            return 0;
        }
        /* Malformed — fall through and regenerate (but warn). */
        fprintf(stderr, "identity: %s is malformed, regenerating\n", path);
    }

    /* Generate fresh. */
    if (gen_ed25519(out->signing_seed, out->signing_pk) < 0) {
        fprintf(stderr, "Ed25519 keygen failed\n");
        return -1;
    }
    if (gen_x25519(out->noise_sk, out->noise_pk) < 0) {
        fprintf(stderr, "X25519 keygen failed\n");
        return -1;
    }
    derive_peer_id(out->noise_pk, out->peer_id);

    /* Serialize + write. */
    uint8_t outbuf[FILE_SIZE];
    size_t off = 0;
    memcpy(outbuf + off, MAGIC, 4); off += 4;
    outbuf[off++] = FILE_VERSION;
    memcpy(outbuf + off, out->signing_seed, BC_ID_SIGNING_SEED); off += BC_ID_SIGNING_SEED;
    memcpy(outbuf + off, out->signing_pk,   BC_ID_SIGNING_PK);   off += BC_ID_SIGNING_PK;
    memcpy(outbuf + off, out->noise_sk,     BC_ID_NOISE_SK);     off += BC_ID_NOISE_SK;
    memcpy(outbuf + off, out->noise_pk,     BC_ID_NOISE_PK);     off += BC_ID_NOISE_PK;

    if (write_file(path, outbuf, off) < 0) {
        fprintf(stderr, "identity: failed to write %s\n", path);
        return -1;
    }
    fprintf(stderr, "identity: generated new keys at %s\n", path);
    return 0;
}

int bc_identity_sign(const bc_identity_t *id,
                     const uint8_t *msg, size_t mlen,
                     uint8_t sig_out[BC_ID_SIGNATURE]) {
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL,
                                                  id->signing_seed, BC_ID_SIGNING_SEED);
    if (!pkey) return -1;
    int rc = -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) goto done;
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) <= 0) goto done;
    size_t siglen = BC_ID_SIGNATURE;
    if (EVP_DigestSign(ctx, sig_out, &siglen, msg, mlen) <= 0) goto done;
    if (siglen != BC_ID_SIGNATURE) goto done;
    rc = 0;
done:
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

int bc_identity_verify(const uint8_t pubkey[BC_ID_SIGNING_PK],
                       const uint8_t sig[BC_ID_SIGNATURE],
                       const uint8_t *msg, size_t mlen) {
    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL,
                                                 pubkey, BC_ID_SIGNING_PK);
    if (!pkey) return -1;
    int rc = -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) goto done;
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) <= 0) goto done;
    int v = EVP_DigestVerify(ctx, sig, BC_ID_SIGNATURE, msg, mlen);
    rc = (v == 1) ? 1 : 0;
done:
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

void bc_identity_peer_id_hex(const bc_identity_t *id, char buf[17]) {
    static const char tab[] = "0123456789abcdef";
    for (int i = 0; i < BC_ID_PEER_ID; i++) {
        buf[i * 2]     = tab[(id->peer_id[i] >> 4) & 0xf];
        buf[i * 2 + 1] = tab[id->peer_id[i] & 0xf];
    }
    buf[16] = '\0';
}
