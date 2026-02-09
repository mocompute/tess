#include "alloc.h"
#include "manifest.h"
#include "platform.h"

#include "str.h"

#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#ifdef MOS_WINDOWS
#include <io.h>
#define ftruncate(fd, size) _chsize(fd, size)
#define fileno              _fileno
#else
#include <unistd.h>
#endif

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

static char temp_dir[512];

static void init_temp_dir(void) {
    platform_temp_dir(temp_dir, sizeof(temp_dir));
}

static void make_temp_path(char *buf, size_t bufsize, char const *filename) {
    snprintf(buf, bufsize, "%s%s", temp_dir, filename);
}

static int write_file(char const *path, char const *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    size_t len = strlen(content);
    if (len != fwrite(content, 1, len, f)) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

// Helper: write package.tl content to temp file, parse, return result
static int parse_pkg(allocator *alloc, char const *content, tl_package *out) {
    char path[512];
    make_temp_path(path, sizeof(path), "test_package.tl");
    if (write_file(path, content)) return -1;
    return tl_package_parse_file(alloc, path, out);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static int test_basic_package(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"Foo\")\n"
                                     "version(\"1.0\")\n"
                                     "author(\"Alice\")\n"
                                     "export(\"Mod1\", \"Mod2\")\n"
                                     "depend(\"Bar\", \"2.0\")\n"
                                     "depend_path(\"./libs\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.info.format != 1;
    error += !str_eq(pkg.info.name, S("Foo"));
    error += !str_eq(pkg.info.version, S("1.0"));
    error += !str_eq(pkg.info.author, S("Alice"));
    error += pkg.info.export_count != 2;
    if (pkg.info.export_count == 2) {
        error += !str_eq(pkg.info.exports[0], S("Mod1"));
        error += !str_eq(pkg.info.exports[1], S("Mod2"));
    }
    error += pkg.dep_count != 1;
    if (pkg.dep_count == 1) {
        error += !str_eq(pkg.deps[0].name, S("Bar"));
        error += !str_eq(pkg.deps[0].version, S("2.0"));
    }
    error += pkg.info.depend_path_count != 1;
    if (pkg.info.depend_path_count == 1) {
        error += !str_eq(pkg.info.depend_paths[0], S("./libs"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_basic_package\n", error);

error:
    arena_destroy(&alloc);
    return error;
}

static int test_minimal_package(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"Foo\")\n"
                                     "version(\"1.0\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.info.format != 1;
    error += !str_eq(pkg.info.name, S("Foo"));
    error += !str_eq(pkg.info.version, S("1.0"));
    error += !str_is_empty(pkg.info.author);
    error += pkg.info.export_count != 0;
    error += pkg.info.depend_path_count != 0;
    error += pkg.dep_count != 0;
    error += pkg.optional_dep_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed in test_minimal_package\n", error);

error:
    arena_destroy(&alloc);
    return error;
}

static int test_multiple_depends(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"App\")\n"
                                     "version(\"0.1\")\n"
                                     "depend(\"Lib1\", \"1.0\")\n"
                                     "depend(\"Lib2\", \"2.0\")\n"
                                     "depend_optional(\"OptLib\", \"3.0\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.dep_count != 2;
    if (pkg.dep_count == 2) {
        error += !str_eq(pkg.deps[0].name, S("Lib1"));
        error += !str_eq(pkg.deps[0].version, S("1.0"));
        error += !str_eq(pkg.deps[1].name, S("Lib2"));
        error += !str_eq(pkg.deps[1].version, S("2.0"));
    }
    error += pkg.optional_dep_count != 1;
    if (pkg.optional_dep_count == 1) {
        error += !str_eq(pkg.optional_deps[0].name, S("OptLib"));
        error += !str_eq(pkg.optional_deps[0].version, S("3.0"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_multiple_depends\n", error);
error:
    arena_destroy(&alloc);
    return error;
}

static int test_depend_with_path(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"App\")\n"
                                     "version(\"0.1\")\n"
                                     "depend(\"Lib\", \"1.0\", \"./path/Lib.tlib\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.dep_count != 1;
    if (pkg.dep_count == 1) {
        error += !str_eq(pkg.deps[0].name, S("Lib"));
        error += !str_eq(pkg.deps[0].version, S("1.0"));
        error += !str_eq(pkg.deps[0].path, S("./path/Lib.tlib"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_depend_with_path\n", error);

error:
    arena_destroy(&alloc);
    return error;
}

static int test_multiple_depend_paths(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"App\")\n"
                                     "version(\"0.1\")\n"
                                     "depend_path(\"./libs\")\n"
                                     "depend_path(\"./vendor\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.info.depend_path_count != 2;
    if (pkg.info.depend_path_count == 2) {
        error += !str_eq(pkg.info.depend_paths[0], S("./libs"));
        error += !str_eq(pkg.info.depend_paths[1], S("./vendor"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_multiple_depend_paths\n", error);
error:
    arena_destroy(&alloc);
    return error;
}

static int test_export_multiple_args(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"App\")\n"
                                     "version(\"0.1\")\n"
                                     "export(\"A\", \"B\", \"C\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.info.export_count != 3;
    if (pkg.info.export_count == 3) {
        error += !str_eq(pkg.info.exports[0], S("A"));
        error += !str_eq(pkg.info.exports[1], S("B"));
        error += !str_eq(pkg.info.exports[2], S("C"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_export_multiple_args\n", error);
error:
    arena_destroy(&alloc);
    return error;
}

static int test_export_multiple_lines(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"App\")\n"
                                     "version(\"0.1\")\n"
                                     "export(\"A\", \"B\")\n"
                                     "export(\"C\")\n"
                                     "export(\"D\", \"E\", \"F\")\n",
                              &pkg);

    error += rc != 0;
    if (rc) goto error;

    error += pkg.info.export_count != 6;
    if (pkg.info.export_count == 6) {
        error += !str_eq(pkg.info.exports[0], S("A"));
        error += !str_eq(pkg.info.exports[1], S("B"));
        error += !str_eq(pkg.info.exports[2], S("C"));
        error += !str_eq(pkg.info.exports[3], S("D"));
        error += !str_eq(pkg.info.exports[4], S("E"));
        error += !str_eq(pkg.info.exports[5], S("F"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_export_multiple_lines\n", error);
error:
    arena_destroy(&alloc);
    return error;
}

static int test_missing_format(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    // No format() call
    int rc = parse_pkg(alloc,
                       "package(\"Foo\")\n"
                       "version(\"1.0\")\n",
                       &pkg);

    // Should fail
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_format\n", error);
    arena_destroy(&alloc);
    return error;
}

static int test_format_not_first(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "package(\"Foo\")\n"
                                     "format(1)\n"
                                     "version(\"1.0\")\n",
                              &pkg);

    // Should fail: format not first
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_format_not_first\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_unsupported_format(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(2)\n"
                                     "package(\"Foo\")\n"
                                     "version(\"1.0\")\n",
                              &pkg);

    // Should fail: unsupported format
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_unsupported_format\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_missing_package(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "version(\"1.0\")\n",
                              &pkg);

    // Should fail
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_package\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_missing_version(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"Foo\")\n",
                              &pkg);

    // Should fail
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_version\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_unknown_function(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"Foo\")\n"
                                     "version(\"1.0\")\n"
                                     "unknown(\"x\")\n",
                              &pkg);

    // Should not fail: just ignore unknown functions
    error += rc != 0;

    if (error) fprintf(stderr, "  %d check(s) failed in test_unknown_function\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_wrong_arg_count(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"a\", \"b\")\n"
                                     "version(\"1.0\")\n",
                              &pkg);

    // Should fail: package expects 1 arg
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_wrong_arg_count\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_non_string_arg(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(42)\n"
                                     "version(\"1.0\")\n",
                              &pkg);

    // Should fail: integer where string expected
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_non_string_arg\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_duplicate_package(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"Foo\")\n"
                                     "version(\"1.0\")\n"
                                     "package(\"Bar\")\n",
                              &pkg);

    // Should fail: duplicate package()
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_duplicate_package\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_duplicate_version(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;

    int        rc = parse_pkg(alloc,
                              "format(1)\n"
                                     "package(\"Foo\")\n"
                                     "version(\"1.0\")\n"
                                     "version(\"2.0\")\n",
                              &pkg);

    // Should fail: duplicate version()
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_duplicate_version\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_missing_file(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 1024);
    tl_package pkg;
    char       path[512];

    make_temp_path(path, sizeof(path), "nonexistent_package.tl");

    int rc = tl_package_parse_file(default_allocator(), path, &pkg);

    // Should fail: file does not exist
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_file\n", error);

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
    init_temp_dir();
    int error      = 0;
    int this_error = 0;

    T(test_basic_package)
    T(test_minimal_package)
    T(test_multiple_depends)
    T(test_depend_with_path)
    T(test_multiple_depend_paths)
    T(test_export_multiple_args)
    T(test_export_multiple_lines)
    T(test_missing_format)
    T(test_format_not_first)
    T(test_unsupported_format)
    T(test_missing_package)
    T(test_missing_version)
    T(test_unknown_function)
    T(test_wrong_arg_count)
    T(test_non_string_arg)
    T(test_duplicate_package)
    T(test_duplicate_version)
    T(test_missing_file)

    if (error) fprintf(stderr, "manifest tests: %d FAILED\n", error);
    return error;
}
