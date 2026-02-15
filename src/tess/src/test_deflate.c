#include "libdeflate.h"
#include <stdio.h>
#include <string.h>

static int test_roundtrip(void) {
    const char                   *input     = "Hello, libdeflate! This is a roundtrip test. "
                                              "Adding some repeated content for better compression: "
                                              "abcabcabcabcabcabcabcabcabcabcabcabcabcabcabcabc";
    size_t                        input_len = strlen(input);

    struct libdeflate_compressor *c         = libdeflate_alloc_compressor(6);
    if (!c) {
        fprintf(stderr, "FAILED: could not allocate compressor\n");
        return 1;
    }

    size_t        bound = libdeflate_deflate_compress_bound(c, input_len);
    unsigned char compressed[4096];
    if (bound > sizeof(compressed)) {
        fprintf(stderr, "FAILED: compress bound too large\n");
        libdeflate_free_compressor(c);
        return 1;
    }

    size_t compressed_len =
      libdeflate_deflate_compress(c, input, input_len, compressed, sizeof(compressed));
    libdeflate_free_compressor(c);
    if (compressed_len == 0) {
        fprintf(stderr, "FAILED: compression returned 0\n");
        return 1;
    }

    struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
    if (!d) {
        fprintf(stderr, "FAILED: could not allocate decompressor\n");
        return 1;
    }

    char                   output[4096];
    size_t                 actual_out;
    enum libdeflate_result r =
      libdeflate_deflate_decompress(d, compressed, compressed_len, output, input_len, &actual_out);
    libdeflate_free_decompressor(d);

    if (r != LIBDEFLATE_SUCCESS) {
        fprintf(stderr, "FAILED: decompression failed with code %d\n", r);
        return 1;
    }
    if (actual_out != input_len) {
        fprintf(stderr, "FAILED: decompressed size %zu != input size %zu\n", actual_out, input_len);
        return 1;
    }
    if (memcmp(input, output, input_len) != 0) {
        fprintf(stderr, "FAILED: decompressed data does not match input\n");
        return 1;
    }

    return 0;
}

int main(void) {
    int error = 0;
    error += test_roundtrip();
    return error;
}
