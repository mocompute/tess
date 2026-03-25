#include "sha256.h"

#include <stdio.h>
#include <string.h>

// https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/SHA256.pdf

/* -------------------------------------------------------------------------- */
/* Helper: compare digest against expected hex string                         */
/* -------------------------------------------------------------------------- */

static int digest_matches(byte const digest[SHA256_DIGEST_SIZE], char const *expected_hex) {
    char hex[SHA256_HEX_SIZE];
    for (int i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[64] = '\0';
    return strcmp(hex, expected_hex) == 0 ? 0 : 1;
}

/* -------------------------------------------------------------------------- */
/* Test: SHA-256 of empty string                                              */
/* -------------------------------------------------------------------------- */

static int test_empty(void) {
    int        error = 0;
    sha256_ctx ctx;
    byte       digest[SHA256_DIGEST_SIZE];

    sha256_init(&ctx);
    sha256_update(&ctx, "", 0);
    sha256_final(&ctx, digest);

    if (digest_matches(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
        fprintf(stderr, "  empty string hash mismatch\n");
        error++;
    }

    return error;
}

/* -------------------------------------------------------------------------- */
/* Test: SHA-256 of "abc"                                                     */
/* -------------------------------------------------------------------------- */

static int test_abc(void) {
    int        error = 0;
    sha256_ctx ctx;
    byte       digest[SHA256_DIGEST_SIZE];

    sha256_init(&ctx);
    sha256_update(&ctx, "abc", 3);
    sha256_final(&ctx, digest);

    if (digest_matches(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        fprintf(stderr, "  'abc' hash mismatch\n");
        error++;
    }

    return error;
}

/* -------------------------------------------------------------------------- */
/* Test: SHA-256 of 448-bit message (two blocks)                              */
/* -------------------------------------------------------------------------- */

static int test_two_blocks(void) {
    int         error = 0;
    sha256_ctx  ctx;
    byte        digest[SHA256_DIGEST_SIZE];
    char const *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    sha256_init(&ctx);
    sha256_update(&ctx, msg, strlen(msg));
    sha256_final(&ctx, digest);

    if (digest_matches(digest, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")) {
        fprintf(stderr, "  two-block message hash mismatch\n");
        error++;
    }

    return error;
}

/* -------------------------------------------------------------------------- */
/* Test: multi-update produces same result as single update                   */
/* -------------------------------------------------------------------------- */

static int test_multi_update(void) {
    int        error = 0;
    sha256_ctx ctx;
    byte       digest_single[SHA256_DIGEST_SIZE];
    byte       digest_multi[SHA256_DIGEST_SIZE];

    /* Single update */
    sha256_init(&ctx);
    sha256_update(&ctx, "abc", 3);
    sha256_final(&ctx, digest_single);

    /* Three separate updates */
    sha256_init(&ctx);
    sha256_update(&ctx, "a", 1);
    sha256_update(&ctx, "b", 1);
    sha256_update(&ctx, "c", 1);
    sha256_final(&ctx, digest_multi);

    if (memcmp(digest_single, digest_multi, SHA256_DIGEST_SIZE) != 0) {
        fprintf(stderr, "  multi-update result differs from single update\n");
        error++;
    }

    return error;
}

/* -------------------------------------------------------------------------- */
/* Test: sha256_hex format                                                    */
/* -------------------------------------------------------------------------- */

static int test_hex_format(void) {
    int  error = 0;
    char out[SHA256_HEX_SIZE + 7];

    sha256_hex("abc", 3, out);

    /* Must start with "sha256:" */
    if (strncmp(out, "sha256:", 7) != 0) {
        fprintf(stderr, "  hex output missing 'sha256:' prefix\n");
        error++;
    }

    /* Verify the hex portion matches expected hash */
    if (strcmp(out + 7, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0) {
        fprintf(stderr, "  hex output hash mismatch: got '%s'\n", out + 7);
        error++;
    }

    /* Verify total length: 7 ("sha256:") + 64 (hex) = 71 */
    if (strlen(out) != 71) {
        fprintf(stderr, "  hex output unexpected length: %zu\n", strlen(out));
        error++;
    }

    return error;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_empty);
    T(test_abc);
    T(test_two_blocks);
    T(test_multi_update);
    T(test_hex_format);

    return error;
}
