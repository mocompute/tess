#include "sha256.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* SHA-256 constants (FIPS 180-4)                                             */
/* -------------------------------------------------------------------------- */

static u32 const K[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static u32 const H_INIT[8] = {
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

/* -------------------------------------------------------------------------- */
/* Bit manipulation helpers                                                   */
/* -------------------------------------------------------------------------- */

static inline u32 rotr(u32 x, u32 n) {
    return (x >> n) | (x << (32 - n));
}

static inline u32 Ch(u32 x, u32 y, u32 z) {
    return (x & y) ^ (~x & z);
}
static inline u32 Maj(u32 x, u32 y, u32 z) {
    return (x & y) ^ (x & z) ^ (y & z);
}
static inline u32 Sigma0(u32 x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}
static inline u32 Sigma1(u32 x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}
static inline u32 sigma0(u32 x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}
static inline u32 sigma1(u32 x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

/* -------------------------------------------------------------------------- */
/* Big-endian load / store                                                    */
/* -------------------------------------------------------------------------- */

static inline u32 load_be32(byte const *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static inline void store_be32(byte *p, u32 v) {
    p[0] = (byte)(v >> 24);
    p[1] = (byte)(v >> 16);
    p[2] = (byte)(v >> 8);
    p[3] = (byte)(v);
}

static inline void store_be64(byte *p, u64 v) {
    p[0] = (byte)(v >> 56);
    p[1] = (byte)(v >> 48);
    p[2] = (byte)(v >> 40);
    p[3] = (byte)(v >> 32);
    p[4] = (byte)(v >> 24);
    p[5] = (byte)(v >> 16);
    p[6] = (byte)(v >> 8);
    p[7] = (byte)(v);
}

/* -------------------------------------------------------------------------- */
/* Transform: process one 64-byte block                                       */
/* -------------------------------------------------------------------------- */

static void sha256_transform(u32 state[8], byte const block[64]) {
    u32 W[64];
    u32 a, b, c, d, e, f, g, h;

    /* Prepare message schedule */
    for (int t = 0; t < 16; ++t) {
        W[t] = load_be32(block + t * 4);
    }
    for (int t = 16; t < 64; ++t) {
        W[t] = sigma1(W[t - 2]) + W[t - 7] + sigma0(W[t - 15]) + W[t - 16];
    }

    /* Initialize working variables */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    /* 64 rounds */
    for (int t = 0; t < 64; ++t) {
        u32 T1 = h + Sigma1(e) + Ch(e, f, g) + K[t] + W[t];
        u32 T2 = Sigma0(a) + Maj(a, b, c);
        h      = g;
        g      = f;
        f      = e;
        e      = d + T1;
        d      = c;
        c      = b;
        b      = a;
        a      = T1 + T2;
    }

    /* Update state */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void sha256_init(sha256_ctx *ctx) {
    memcpy(ctx->state, H_INIT, sizeof(H_INIT));
    ctx->bitcount = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha256_update(sha256_ctx *ctx, void const *data, size_t len) {
    byte const *src    = (byte const *)data;
    size_t      bufpos = (size_t)((ctx->bitcount >> 3) & 0x3F);

    ctx->bitcount += (u64)len << 3;

    /* If we have buffered data, try to complete a block */
    if (bufpos > 0) {
        size_t space = 64 - bufpos;
        if (len >= space) {
            memcpy(ctx->buffer + bufpos, src, space);
            sha256_transform(ctx->state, ctx->buffer);
            src += space;
            len -= space;
            bufpos = 0;
        } else {
            memcpy(ctx->buffer + bufpos, src, len);
            return;
        }
    }

    /* Process full blocks directly from input */
    while (len >= 64) {
        sha256_transform(ctx->state, src);
        src += 64;
        len -= 64;
    }

    /* Buffer remaining bytes */
    if (len > 0) {
        memcpy(ctx->buffer, src, len);
    }
}

void sha256_final(sha256_ctx *ctx, byte digest[SHA256_DIGEST_SIZE]) {
    size_t bufpos = (size_t)((ctx->bitcount >> 3) & 0x3F);

    /* Append 0x80 byte */
    ctx->buffer[bufpos++] = 0x80;

    /* If not enough room for the 8-byte length, pad and process */
    if (bufpos > 56) {
        memset(ctx->buffer + bufpos, 0, 64 - bufpos);
        sha256_transform(ctx->state, ctx->buffer);
        bufpos = 0;
    }

    /* Pad with zeros up to byte 56, then append bit length */
    memset(ctx->buffer + bufpos, 0, 56 - bufpos);
    store_be64(ctx->buffer + 56, ctx->bitcount);
    sha256_transform(ctx->state, ctx->buffer);

    /* Produce digest in big-endian */
    for (int i = 0; i < 8; ++i) {
        store_be32(digest + i * 4, ctx->state[i]);
    }
}

void sha256_hex(void const *data, size_t len, char *out) {
    byte       digest[SHA256_DIGEST_SIZE];
    sha256_ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);

    static char const hex[] = "0123456789abcdef";
    memcpy(out, "sha256:", 7);
    for (int i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        out[7 + i * 2]     = hex[digest[i] >> 4];
        out[7 + i * 2 + 1] = hex[digest[i] & 0xf];
    }
    out[7 + 64] = '\0';
}
