#include "alloc.h"
#include "lockfile.h"
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

// Helper: write lock file content to temp file, parse, return result
static int parse_lock(allocator *alloc, char const *content, tl_lockfile *out) {
    char path[512];
    make_temp_path(path, sizeof(path), "test_package.tl.lock");
    if (write_file(path, content)) return -1;
    return tl_lockfile_parse_file(alloc, path, out);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static int test_basic_lockfile(void) {
    int         error = 0;
    allocator  *alloc = arena_create(default_allocator(), 1024);
    tl_lockfile lf;

    int         rc =
      parse_lock(alloc,
                 "lock_format(1)\n"
                 "\n"
                 "locked(MathUtils,  \"1.0.0\", \"https://example.com/packages/\", \"sha256:abc123\")\n"
                 "locked(LoggingLib, \"2.0.0\", \"https://example.com/packages/\", \"sha256:def456\")\n"
                 "\n"
                 "needs(MathUtils, \"1.0.0\", LoggingLib, \"2.0.0\")\n",
                 &lf);

    error += rc != 0;
    if (rc) goto done;

    error += lf.format != 1;
    error += lf.dep_count != 2;
    if (lf.dep_count == 2) {
        error += !str_eq(lf.deps[0].name, S("MathUtils"));
        error += !str_eq(lf.deps[0].version, S("1.0.0"));
        error += !str_eq(lf.deps[0].base_url, S("https://example.com/packages/"));
        error += !str_eq(lf.deps[0].hash, S("sha256:abc123"));

        error += !str_eq(lf.deps[1].name, S("LoggingLib"));
        error += !str_eq(lf.deps[1].version, S("2.0.0"));
        error += !str_eq(lf.deps[1].base_url, S("https://example.com/packages/"));
        error += !str_eq(lf.deps[1].hash, S("sha256:def456"));
    }
    error += lf.edge_count != 1;
    if (lf.edge_count == 1) {
        error += !str_eq(lf.edges[0].name, S("MathUtils"));
        error += !str_eq(lf.edges[0].version, S("1.0.0"));
        error += !str_eq(lf.edges[0].dep_name, S("LoggingLib"));
        error += !str_eq(lf.edges[0].dep_version, S("2.0.0"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_basic_lockfile\n", error);

done:
    arena_destroy(&alloc);
    return error;
}

static int test_lockfile_no_edges(void) {
    int         error = 0;
    allocator  *alloc = arena_create(default_allocator(), 1024);
    tl_lockfile lf;

    int         rc = parse_lock(alloc,
                                "lock_format(1)\n"
                                        "locked(Foo, \"1.0.0\", \"https://example.com/\", \"sha256:aabbcc\")\n"
                                        "locked(Bar, \"2.0.0\", \"https://example.com/\", \"sha256:ddeeff\")\n",
                                &lf);

    error += rc != 0;
    if (rc) goto done;

    error += lf.format != 1;
    error += lf.dep_count != 2;
    error += lf.edge_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed in test_lockfile_no_edges\n", error);

done:
    arena_destroy(&alloc);
    return error;
}

static int test_lockfile_missing_format(void) {
    int         error = 0;
    allocator  *alloc = arena_create(default_allocator(), 1024);
    tl_lockfile lf;

    int rc = parse_lock(alloc, "locked(Foo, \"1.0.0\", \"https://example.com/\", \"sha256:abc\")\n", &lf);

    // Should fail: no lock_format()
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_lockfile_missing_format\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_lockfile_wrong_format(void) {
    int         error = 0;
    allocator  *alloc = arena_create(default_allocator(), 1024);
    tl_lockfile lf;

    int         rc = parse_lock(alloc,
                                "lock_format(2)\n"
                                        "locked(Foo, \"1.0.0\", \"https://example.com/\", \"sha256:abc\")\n",
                                &lf);

    // Should fail: unsupported format version
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_lockfile_wrong_format\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_lockfile_bad_locked_argc(void) {
    int         error = 0;
    allocator  *alloc = arena_create(default_allocator(), 1024);
    tl_lockfile lf;

    int         rc = parse_lock(alloc,
                                "lock_format(1)\n"
                                        "locked(Foo, \"1.0\")\n",
                                &lf);

    // Should fail: too few args
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_lockfile_bad_locked_argc\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_lockfile_bad_needs_argc(void) {
    int         error = 0;
    allocator  *alloc = arena_create(default_allocator(), 1024);
    tl_lockfile lf;

    int         rc = parse_lock(alloc,
                                "lock_format(1)\n"
                                        "needs(Foo, \"1.0\", Bar)\n",
                                &lf);

    // Should fail: too few args
    error += rc != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_lockfile_bad_needs_argc\n", error);

    arena_destroy(&alloc);
    return error;
}

static int test_lockfile_roundtrip(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    // Build test data
    tl_locked_dep deps[2] = {
      {
        .name     = S("Alpha"),
        .version  = S("1.0.0"),
        .base_url = S("https://example.com/packages/"),
        .hash     = S("sha256:1111111111111111111111111111111111111111111111111111111111111111"),
      },
      {
        .name     = S("Beta"),
        .version  = S("2.0.0"),
        .base_url = S("https://example.com/packages/"),
        .hash     = S("sha256:2222222222222222222222222222222222222222222222222222222222222222"),
      },
    };

    tl_lock_edge edges[1] = {
      {
        .name        = S("Alpha"),
        .version     = S("1.0.0"),
        .dep_name    = S("Beta"),
        .dep_version = S("2.0.0"),
      },
    };

    // Write lock file
    char path[512];
    make_temp_path(path, sizeof(path), "test_roundtrip.tl.lock");

    int write_rc = tl_lockfile_write(path, deps, 2, edges, 1);
    error += write_rc != 0;
    if (write_rc) goto done;

    // Parse it back
    tl_lockfile lf;
    int         parse_rc = tl_lockfile_parse_file(alloc, path, &lf);
    error += parse_rc != 0;
    if (parse_rc) goto done;

    // Verify fields
    error += lf.format != 1;
    error += lf.dep_count != 2;
    if (lf.dep_count == 2) {
        error += !str_eq(lf.deps[0].name, S("Alpha"));
        error += !str_eq(lf.deps[0].version, S("1.0.0"));
        error += !str_eq(lf.deps[0].base_url, S("https://example.com/packages/"));
        error += !str_eq(lf.deps[0].hash,
                         S("sha256:1111111111111111111111111111111111111111111111111111111111111111"));

        error += !str_eq(lf.deps[1].name, S("Beta"));
        error += !str_eq(lf.deps[1].version, S("2.0.0"));
        error += !str_eq(lf.deps[1].base_url, S("https://example.com/packages/"));
        error += !str_eq(lf.deps[1].hash,
                         S("sha256:2222222222222222222222222222222222222222222222222222222222222222"));
    }
    error += lf.edge_count != 1;
    if (lf.edge_count == 1) {
        error += !str_eq(lf.edges[0].name, S("Alpha"));
        error += !str_eq(lf.edges[0].version, S("1.0.0"));
        error += !str_eq(lf.edges[0].dep_name, S("Beta"));
        error += !str_eq(lf.edges[0].dep_version, S("2.0.0"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_lockfile_roundtrip\n", error);

done:
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

    T(test_basic_lockfile)
    T(test_lockfile_no_edges)
    T(test_lockfile_missing_format)
    T(test_lockfile_wrong_format)
    T(test_lockfile_bad_locked_argc)
    T(test_lockfile_bad_needs_argc)
    T(test_lockfile_roundtrip)

    if (error) fprintf(stderr, "lockfile tests: %d FAILED\n", error);
    return error;
}
