#include "file.h"

#include <stdio.h>
#include <string.h>

static int test_file_exe_directory(void) {
    int  error = 0;
    char buf[4096];
    span s = {.buf = buf, .len = sizeof(buf)};

    // Test: function should return non-NULL
    char *result = file_exe_directory(s);
    if (!result) {
        fprintf(stderr, "file_exe_directory returned NULL\n");
        error++;
    }

    // Test: result should be a non-empty path
    if (result && strlen(result) == 0) {
        fprintf(stderr, "file_exe_directory returned empty string\n");
        error++;
    }

    // Test: result should not contain the executable name
    // (should be directory only, not full path)
    if (result) {
        char *last_slash = strrchr(result, '/');
#ifdef MOS_WINDOWS
        char *last_backslash = strrchr(result, '\\');
        if (last_backslash > last_slash) last_slash = last_backslash;
#endif
        // The result should be a directory, so it shouldn't end with
        // test_mos_file (or test_mos_file.exe on Windows)
        if (last_slash && strstr(last_slash, "test_mos_file")) {
            fprintf(stderr, "file_exe_directory should return directory, not full path\n");
            error++;
        }
    }

    return error;
}

#define T(name)                                                                \
    this_error = name();                                                       \
    if (this_error) {                                                          \
        fprintf(stderr, "FAILED: %s\n", #name);                                \
        error += this_error;                                                   \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_file_exe_directory)

    return error;
}
