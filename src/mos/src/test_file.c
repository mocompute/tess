#include "file.h"

#include <stdio.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <direct.h>
#define platform_getcwd _getcwd
#else
#include <unistd.h>
#define platform_getcwd getcwd
#endif

static int test_file_path_relative(void) {
    int        error = 0;
    allocator *alloc = default_allocator();

    // Get current working directory for absolute path tests
    char cwd[4096];
    if (!platform_getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "  failed to get cwd\n");
        return 1;
    }

    // Test 1: Same directory - should return "."
    {
        str result = file_path_relative(alloc, "/a/b/c", "/a/b/c");
        if (!str_eq(result, S("."))) {
            fprintf(stderr, "  same path should return '.', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 2: File in same directory
    {
        str result = file_path_relative(alloc, "/a/b", "/a/b/file.txt");
        if (!str_eq(result, S("file.txt"))) {
            fprintf(stderr, "  file in same dir: expected 'file.txt', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 3: File in parent directory
    {
        str result = file_path_relative(alloc, "/a/b/c", "/a/b/file.txt");
        if (!str_eq(result, S("../file.txt"))) {
            fprintf(stderr, "  file in parent: expected '../file.txt', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 4: File in sibling directory
    {
        str result = file_path_relative(alloc, "/a/b/c", "/a/b/d/file.txt");
        if (!str_eq(result, S("../d/file.txt"))) {
            fprintf(stderr, "  file in sibling: expected '../d/file.txt', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 5: File two levels up
    {
        str result = file_path_relative(alloc, "/a/b/c/d", "/a/b/file.txt");
        if (!str_eq(result, S("../../file.txt"))) {
            fprintf(stderr, "  file two levels up: expected '../../file.txt', got '%s'\n",
                    str_cstr(&result));
            error++;
        }
    }

    // Test 6: Relative paths (uses cwd internally)
    {
        str result = file_path_relative(alloc, "src", "src/file.c");
        if (str_is_empty(result)) {
            fprintf(stderr, "  relative paths should work, got empty\n");
            error++;
        } else if (!str_eq(result, S("file.c"))) {
            fprintf(stderr, "  relative path: expected 'file.c', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 7: Empty input should return empty
    {
        str result = file_path_relative(alloc, "", "/a/b");
        if (!str_is_empty(result)) {
            fprintf(stderr, "  empty from_dir should return empty, got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 8: Deeply nested relative path
    {
        str result = file_path_relative(alloc, "/a/b/c", "/a/x/y/z/file.txt");
        if (!str_eq(result, S("../../x/y/z/file.txt"))) {
            fprintf(stderr, "  deep relative: expected '../../x/y/z/file.txt', got '%s'\n",
                    str_cstr(&result));
            error++;
        }
    }

    return error;
}

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

    T(test_file_path_relative)
    T(test_file_exe_directory)

    return error;
}
