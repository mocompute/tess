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

static int test_file_path_normalize(void) {
    int        error = 0;
    allocator *alloc = default_allocator();

    // Test 1: Simple path unchanged
    {
        str result = file_path_normalize(alloc, S("/a/b/c"));
        if (!str_eq(result, S("/a/b/c"))) {
            fprintf(stderr, "  simple path: expected '/a/b/c', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 2: Remove single dot components
    {
        str result = file_path_normalize(alloc, S("/a/./b/./c"));
        if (!str_eq(result, S("/a/b/c"))) {
            fprintf(stderr, "  dot removal: expected '/a/b/c', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 3: Resolve parent directory references
    {
        str result = file_path_normalize(alloc, S("/a/b/../c"));
        if (!str_eq(result, S("/a/c"))) {
            fprintf(stderr, "  dotdot resolution: expected '/a/c', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 4: Multiple parent references
    {
        str result = file_path_normalize(alloc, S("/a/b/c/../../d"));
        if (!str_eq(result, S("/a/d"))) {
            fprintf(stderr, "  multiple dotdot: expected '/a/d', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 5: Redundant separators
    {
        str result = file_path_normalize(alloc, S("/a//b///c"));
        if (!str_eq(result, S("/a/b/c"))) {
            fprintf(stderr, "  redundant sep: expected '/a/b/c', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 6: Relative path with dotdot
    {
        str result = file_path_normalize(alloc, S("a/b/../c"));
        if (!str_eq(result, S("a/c"))) {
            fprintf(stderr, "  relative dotdot: expected 'a/c', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 7: Leading dotdot in relative path
    {
        str result = file_path_normalize(alloc, S("../a/b"));
        if (!str_eq(result, S("../a/b"))) {
            fprintf(stderr, "  leading dotdot: expected '../a/b', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 8: Empty path returns "."
    {
        str result = file_path_normalize(alloc, S(""));
        if (!str_eq(result, S(""))) {
            // Empty input should return empty
            if (!str_is_empty(result)) {
                fprintf(stderr, "  empty path: expected empty, got '%s'\n", str_cstr(&result));
                error++;
            }
        }
    }

    // Test 9: Root path stays root
    {
        str result = file_path_normalize(alloc, S("/"));
        if (!str_eq(result, S("/"))) {
            fprintf(stderr, "  root path: expected '/', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 10: Current directory
    {
        str result = file_path_normalize(alloc, S("."));
        if (!str_eq(result, S("."))) {
            fprintf(stderr, "  current dir: expected '.', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 11: Dotdot at root is ignored
    {
        str result = file_path_normalize(alloc, S("/../a"));
        if (!str_eq(result, S("/a"))) {
            fprintf(stderr, "  dotdot at root: expected '/a', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    return error;
}

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
        str result = file_path_relative(alloc, S("/a/b/c"), S("/a/b/c"));
        if (!str_eq(result, S("."))) {
            fprintf(stderr, "  same path should return '.', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 2: File in same directory
    {
        str result = file_path_relative(alloc, S("/a/b"), S("/a/b/file.txt"));
        if (!str_eq(result, S("file.txt"))) {
            fprintf(stderr, "  file in same dir: expected 'file.txt', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 3: File in parent directory
    {
        str result = file_path_relative(alloc, S("/a/b/c"), S("/a/b/file.txt"));
        if (!str_eq(result, S("../file.txt"))) {
            fprintf(stderr, "  file in parent: expected '../file.txt', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 4: File in sibling directory
    {
        str result = file_path_relative(alloc, S("/a/b/c"), S("/a/b/d/file.txt"));
        if (!str_eq(result, S("../d/file.txt"))) {
            fprintf(stderr, "  file in sibling: expected '../d/file.txt', got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 5: File two levels up
    {
        str result = file_path_relative(alloc, S("/a/b/c/d"), S("/a/b/file.txt"));
        if (!str_eq(result, S("../../file.txt"))) {
            fprintf(stderr, "  file two levels up: expected '../../file.txt', got '%s'\n",
                    str_cstr(&result));
            error++;
        }
    }

    // Test 6: Relative paths (uses cwd internally)
    {
        str result = file_path_relative(alloc, S("src"), S("src/file.c"));
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
        str result = file_path_relative(alloc, S(""), S("/a/b"));
        if (!str_is_empty(result)) {
            fprintf(stderr, "  empty from_dir should return empty, got '%s'\n", str_cstr(&result));
            error++;
        }
    }

    // Test 8: Deeply nested relative path
    {
        str result = file_path_relative(alloc, S("/a/b/c"), S("/a/x/y/z/file.txt"));
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

    T(test_file_path_normalize)
    T(test_file_path_relative)
    T(test_file_exe_directory)

    return error;
}
