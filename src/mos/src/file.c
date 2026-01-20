#include "file.h"
#include "platform.h"
#include "types.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef MOS_APPLE
#include <mach-o/dyld.h>
#endif

int file_exists(char const *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

void file_read(allocator *alloc, char const *filename, char **out, u32 *out_size) {

    *out      = null;
    *out_size = 0;

    FILE *f   = fopen(filename, "rb");
    if (!f) {
        perror("failed to open file");
        fprintf(stderr, "error: %s\n", filename);
        return;
    }

    if (fseek(f, 0, SEEK_END)) {
        perror("seek failed");
        fclose(f);
        return;
    }

    long size = ftell(f);
    if (size < 0) {
        perror("failed to read file position");
        fclose(f);
        return;
    }

    if (fseek(f, 0, SEEK_SET)) {
        perror("failed to rewind");
        fclose(f);
        return;
    }

    char *buf = alloc_malloc(alloc, (size_t)size);
    if (!buf) {
        size = 0;
        goto cleanup;
    }

    size_t const n = fread(buf, 1, (size_t)size, f);
    if (n != (size_t)size || n > UINT32_MAX) {
        perror("failed to read file");
        alloc_free(alloc, buf);
        buf = null;
        goto cleanup;
    }

cleanup:
    fclose(f);
    *out      = buf;
    *out_size = (u32)size;
}

char const *file_basename(char const *input) {
    char const *p1 = strrchr(input, '/');
    char const *p2 = strrchr(input, '\\');
    char const *p  = (p1 > p2) ? p1 : p2;
    return p ? p + 1 : input;
}

char *file_current_working_directory(span buf) {
    char *out;
#ifndef MOS_WINDOWS
    out = getcwd(buf.buf, buf.len);
#else
    if (buf.len > INT_MAX) fatal("overflow");
    out = _getcwd(buf.buf, (int)buf.len);
#endif
    return out;
}

char *file_exe_directory(span buf) {
#ifdef MOS_WINDOWS
    DWORD len = GetModuleFileNameA(NULL, buf.buf, (DWORD)buf.len);
    if (len == 0 || len >= buf.len) return NULL;
#elif defined(MOS_APPLE)
    uint32_t size = (uint32_t)buf.len;
    if (_NSGetExecutablePath(buf.buf, &size) != 0) return NULL;
#else
    ssize_t len = readlink("/proc/self/exe", buf.buf, buf.len - 1);
    if (len == -1) return NULL;
    buf.buf[len] = '\0';
#endif
    // Find last path separator and truncate to get directory
    char *last_sep = strrchr(buf.buf, '/');
#ifdef MOS_WINDOWS
    char *last_sep_win = strrchr(buf.buf, '\\');
    if (last_sep_win > last_sep) last_sep = last_sep_win;
#endif
    if (last_sep) *last_sep = '\0';
    return buf.buf;
}
