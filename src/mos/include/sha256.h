#ifndef MOS_SHA256_H
#define MOS_SHA256_H

#include "types.h"
#include <stddef.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE    65 /* 64 hex chars + NUL */

typedef struct {
    u32  state[8];
    u64  bitcount;
    byte buffer[64];
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, void const *data, size_t len);
void sha256_final(sha256_ctx *ctx, byte digest[SHA256_DIGEST_SIZE]);

/* Convenience: hash data, produce "sha256:<64hex>" string.
   out must be at least SHA256_HEX_SIZE + 7 bytes (7 for "sha256:" prefix). */
void sha256_hex(void const *data, size_t len, char *out);

#endif
