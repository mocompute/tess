#include "file.h"
#include "types.h"

#include <stdio.h>

char *file_read(allocator *alloc, char const *filename) {

    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("failed to open file");
        return null;
    }

    if (fseek(f, 0, SEEK_END)) {
        perror("seek failed");
        fclose(f);
        return null;
    }

    long const size = ftell(f);
    if (size < 0) {
        perror("failed to read file position");
        fclose(f);
        return null;
    }

    if (fseek(f, 0, SEEK_SET)) {
        perror("failed to rewind");
        fclose(f);
        return null;
    }

    char *buf = alloc_malloc(alloc, (size_t)size);
    if (!buf) goto cleanup;

    size_t const n = fread(buf, 1, (size_t)size, f);
    if (n != (size_t)size) {
        perror("failed to read file");
        alloc_free(alloc, buf);
        buf = null;
        goto cleanup;
    }

cleanup:
    fclose(f);
    return buf;
}
