#include "alloc.h"
#include "fetch.h"
#include "file.h"
#include "hashmap.h"
#include "lockfile.h"
#include "platform.h"
#include "str.h"
#include "tpkg.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#ifdef MOS_WINDOWS
#define SEP "\\"
#else
#define SEP "/"
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

static void make_temp_path(char *buf, size_t bufsize, char const *name) {
    snprintf(buf, bufsize, "%s%s" SEP, temp_dir, name);
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

// Create a minimal .tpkg archive and return its bytes (arena-allocated).
static int make_test_tpkg(allocator *alloc, char const *name, char const *version,
                          char const *package_tl_content, char **out_data, u32 *out_size) {
    // Build metadata
    tl_tpkg_metadata meta = {
      .name    = str_init_static(name),
      .version = str_init_static(version),
    };

    // Build entries: a dummy source file + optional package.tl
    tl_tpkg_entry entries[2];
    u32           entry_count = 0;

    char dummy_name[128];
    snprintf(dummy_name, sizeof(dummy_name), "%s.tl", name);
    char dummy_content[128];
    snprintf(dummy_content, sizeof(dummy_content), "#module %s\nval() { 42 }\n", name);

    entries[entry_count++] = (tl_tpkg_entry){
      .name     = dummy_name,
      .name_len = (u32)strlen(dummy_name),
      .data     = (byte const *)dummy_content,
      .data_len = (u32)strlen(dummy_content),
    };

    if (package_tl_content) {
        entries[entry_count++] = (tl_tpkg_entry){
          .name     = "package.tl",
          .name_len = 10,
          .data     = (byte const *)package_tl_content,
          .data_len = (u32)strlen(package_tl_content),
        };
    }

    // Write to temp file
    char tpkg_path[512];
    snprintf(tpkg_path, sizeof(tpkg_path), "%stpkg_tmp_%s_%s.tpkg", temp_dir, name, version);

    if (tl_tpkg_write(alloc, tpkg_path, &meta, entries, entry_count)) {
        return 1;
    }

    // Read back as bytes
    file_read(alloc, tpkg_path, out_data, out_size);
    remove(tpkg_path);
    return *out_data ? 0 : 1;
}

// Register a URL -> data mapping in the mock hashmap.
static void mock_url(hashmap **mock, char const *url, char *data, u32 size) {
    byte_sized val = {.v = (byte *)data, .size = size};
    map_set(mock, url, (u8)strlen(url), &val);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Fresh fetch: one dependency with URL → lock file + saved .tpkg
static int test_fetch_single_dep(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_single");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(FooLib, \"1.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    // Create mock .tpkg
    char *tpkg_data;
    u32   tpkg_size;
    error += make_test_tpkg(alloc, "FooLib", "1.0.0", null, &tpkg_data, &tpkg_size);

    // Set up mock
    hashmap *mock = map_create(alloc, sizeof(byte_sized), 8);
    mock_url(&mock, "http://example.com/FooLib-1.0.0.tpkg", tpkg_data, tpkg_size);
    file_url_get_opts url_opts = {.mock_responses = mock};

    // Run fetch
    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
      .verbose         = 0,
    };
    int rc = tl_fetch(alloc, &opts);
    error += (rc != 0);

    tl_lockfile lockfile = {0};
    int         lock_ok  = tl_lockfile_parse_file(alloc, lock_path, &lockfile);
    error += (lock_ok != 0);
    error += (lockfile.dep_count != 1);
    if (lockfile.dep_count == 1) {
        error += !str_eq(lockfile.deps[0].name, S("FooLib"));
        error += !str_eq(lockfile.deps[0].version, S("1.0.0"));
        error += str_is_empty(lockfile.deps[0].hash);
    }

    char saved[512];
    snprintf(saved, sizeof(saved), "%sFooLib-1.0.0.tpkg", libs);
    str saved_str = str_init_static(saved);
    error += !file_exists(saved_str);

    arena_destroy(&alloc);
    return error;
}

// Two independent dependencies
static int test_fetch_two_deps(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_two");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(Alpha, \"1.0.0\", \"http://example.com/\")\n"
                        "depend(Beta, \"2.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    char *alpha_data, *beta_data;
    u32   alpha_size, beta_size;
    error += make_test_tpkg(alloc, "Alpha", "1.0.0", null, &alpha_data, &alpha_size);
    error += make_test_tpkg(alloc, "Beta", "2.0.0", null, &beta_data, &beta_size);

    hashmap *mock = map_create(alloc, sizeof(byte_sized), 8);
    mock_url(&mock, "http://example.com/Alpha-1.0.0.tpkg", alpha_data, alpha_size);
    mock_url(&mock, "http://example.com/Beta-2.0.0.tpkg", beta_data, beta_size);
    file_url_get_opts url_opts = {.mock_responses = mock};

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
    };
    error += (tl_fetch(alloc, &opts) != 0);

    tl_lockfile lockfile = {0};
    error += (tl_lockfile_parse_file(alloc, lock_path, &lockfile) != 0);
    error += (lockfile.dep_count != 2);

    // Entries are sorted alphabetically
    if (lockfile.dep_count == 2) {
        error += !str_eq(lockfile.deps[0].name, S("Alpha"));
        error += !str_eq(lockfile.deps[1].name, S("Beta"));
    }

    arena_destroy(&alloc);
    return error;
}

// No dependencies → returns success, no lock file
static int test_fetch_no_deps(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512];
    make_temp_path(work, sizeof(work), "fetch_nodeps");
    platform_mkdir(work);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n");

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
    };
    error += (tl_fetch(alloc, &opts) != 0);

    arena_destroy(&alloc);
    return error;
}

// Missing depend_path → returns error
static int test_fetch_no_depend_path(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512];
    make_temp_path(work, sizeof(work), "fetch_nodp");
    platform_mkdir(work);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(FooLib, \"1.0.0\", \"http://example.com/\")\n");

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
    };
    // Should fail because no depend_path
    error += (tl_fetch(alloc, &opts) != 1);

    arena_destroy(&alloc);
    return error;
}

// URL not in mock → download failure
static int test_fetch_download_failure(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_fail");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(Missing, \"1.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    // Empty mock — no URLs registered
    hashmap          *mock    = map_create(alloc, sizeof(byte_sized), 8);
    file_url_get_opts url_opts = {.mock_responses = mock};

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
    };
    error += (tl_fetch(alloc, &opts) != 1);

    arena_destroy(&alloc);
    return error;
}

// Local-only dep (no URL, .tpkg in depend_path)
static int test_fetch_local_only_dep(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_local");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(LocalLib, \"1.0.0\")\n"
                        "depend_path(\"libs\")\n");

    // Create .tpkg and place in libs/
    char *tpkg_data;
    u32   tpkg_size;
    error += make_test_tpkg(alloc, "LocalLib", "1.0.0", null, &tpkg_data, &tpkg_size);

    char tpkg_path[512];
    snprintf(tpkg_path, sizeof(tpkg_path), "%sLocalLib-1.0.0.tpkg", libs);
    error += file_write(tpkg_path, tpkg_data, tpkg_size);

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
    };
    error += (tl_fetch(alloc, &opts) != 0);

    // Verify lock file
    tl_lockfile lockfile = {0};
    error += (tl_lockfile_parse_file(alloc, lock_path, &lockfile) != 0);
    error += (lockfile.dep_count != 1);
    if (lockfile.dep_count == 1) {
        error += !str_eq(lockfile.deps[0].name, S("LocalLib"));
        error += !str_eq(lockfile.deps[0].version, S("1.0.0"));
    }

    arena_destroy(&alloc);
    return error;
}

// Second fetch with valid lock + local files → verify path (no downloads)
static int test_fetch_lock_reuse(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_reuse");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(FooLib, \"1.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    // Create .tpkg
    char *tpkg_data;
    u32   tpkg_size;
    error += make_test_tpkg(alloc, "FooLib", "1.0.0", null, &tpkg_data, &tpkg_size);

    // First fetch with mock
    hashmap *mock = map_create(alloc, sizeof(byte_sized), 8);
    mock_url(&mock, "http://example.com/FooLib-1.0.0.tpkg", tpkg_data, tpkg_size);
    file_url_get_opts url_opts = {.mock_responses = mock};

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
    };
    error += (tl_fetch(alloc, &opts) != 0);

    // Second fetch — .tpkg is now local, no mock needed
    tl_fetch_opts opts2 = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = null, // no mock; shouldn't need network
    };
    error += (tl_fetch(alloc, &opts2) != 0);

    arena_destroy(&alloc);
    return error;
}

// Lock file outdated (new dep added) → re-resolves
static int test_fetch_lock_outdated(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_outdated");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);

    // First: one dependency
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(Alpha, \"1.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    char *alpha_data, *beta_data;
    u32   alpha_size, beta_size;
    error += make_test_tpkg(alloc, "Alpha", "1.0.0", null, &alpha_data, &alpha_size);
    error += make_test_tpkg(alloc, "Beta", "2.0.0", null, &beta_data, &beta_size);

    hashmap *mock = map_create(alloc, sizeof(byte_sized), 8);
    mock_url(&mock, "http://example.com/Alpha-1.0.0.tpkg", alpha_data, alpha_size);
    mock_url(&mock, "http://example.com/Beta-2.0.0.tpkg", beta_data, beta_size);
    file_url_get_opts url_opts = {.mock_responses = mock};

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
    };
    error += (tl_fetch(alloc, &opts) != 0);

    // Lock has 1 dep
    tl_lockfile lockfile = {0};
    error += (tl_lockfile_parse_file(alloc, lock_path, &lockfile) != 0);
    error += (lockfile.dep_count != 1);

    // Add second dependency to package.tl
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(Alpha, \"1.0.0\", \"http://example.com/\")\n"
                        "depend(Beta, \"2.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    // Second fetch should detect outdated lock and re-resolve
    error += (tl_fetch(alloc, &opts) != 0);

    tl_lockfile lockfile2 = {0};
    error += (tl_lockfile_parse_file(alloc, lock_path, &lockfile2) != 0);
    error += (lockfile2.dep_count != 2);

    arena_destroy(&alloc);
    return error;
}

// Archive name mismatch → error
static int test_fetch_archive_name_mismatch(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_mismatch");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(FooLib, \"1.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    // Create .tpkg with WRONG name (BarLib instead of FooLib)
    char *tpkg_data;
    u32   tpkg_size;
    error += make_test_tpkg(alloc, "BarLib", "1.0.0", null, &tpkg_data, &tpkg_size);

    hashmap *mock = map_create(alloc, sizeof(byte_sized), 8);
    mock_url(&mock, "http://example.com/FooLib-1.0.0.tpkg", tpkg_data, tpkg_size);
    file_url_get_opts url_opts = {.mock_responses = mock};

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
    };
    // Should fail: archive name is BarLib but expected FooLib
    error += (tl_fetch(alloc, &opts) != 1);

    arena_destroy(&alloc);
    return error;
}

// Transitive dependency: A depends on B
static int test_fetch_transitive_dep(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char work[512], libs[512];
    make_temp_path(work, sizeof(work), "fetch_trans");
    platform_mkdir(work);
    snprintf(libs, sizeof(libs), "%slibs" SEP, work);
    platform_mkdir(libs);

    char pkg_path[512], lock_path[512];
    snprintf(pkg_path, sizeof(pkg_path), "%spackage.tl", work);
    snprintf(lock_path, sizeof(lock_path), "%spackage.tl.lock", work);
    remove(lock_path);
    error += write_file(pkg_path,
                        "format(1)\n"
                        "package(MyApp)\n"
                        "version(\"0.1.0\")\n"
                        "depend(LibA, \"1.0.0\", \"http://example.com/\")\n"
                        "depend_path(\"libs\")\n");

    // LibA depends on LibB (embedded package.tl)
    char const *liba_pkg = "format(1)\n"
                           "package(LibA)\n"
                           "version(\"1.0.0\")\n"
                           "depend(LibB, \"1.0.0\", \"http://example.com/\")\n"
                           "depend_path(\"libs\")\n";
    char *liba_data, *libb_data;
    u32   liba_size, libb_size;
    error += make_test_tpkg(alloc, "LibA", "1.0.0", liba_pkg, &liba_data, &liba_size);
    error += make_test_tpkg(alloc, "LibB", "1.0.0", null, &libb_data, &libb_size);

    hashmap *mock = map_create(alloc, sizeof(byte_sized), 8);
    mock_url(&mock, "http://example.com/LibA-1.0.0.tpkg", liba_data, liba_size);
    mock_url(&mock, "http://example.com/LibB-1.0.0.tpkg", libb_data, libb_size);
    file_url_get_opts url_opts = {.mock_responses = mock};

    tl_fetch_opts opts = {
      .package_tl_path = pkg_path,
      .lock_path       = lock_path,
      .work_dir        = work,
      .url_opts        = &url_opts,
    };
    error += (tl_fetch(alloc, &opts) != 0);

    tl_lockfile lockfile = {0};
    error += (tl_lockfile_parse_file(alloc, lock_path, &lockfile) != 0);
    error += (lockfile.dep_count != 2);
    if (lockfile.dep_count == 2) {
        // Sorted: LibA, LibB
        error += !str_eq(lockfile.deps[0].name, S("LibA"));
        error += !str_eq(lockfile.deps[1].name, S("LibB"));
    }
    // Should have a dependency edge LibA -> LibB
    error += (lockfile.edge_count != 1);
    if (lockfile.edge_count == 1) {
        error += !str_eq(lockfile.edges[0].name, S("LibA"));
        error += !str_eq(lockfile.edges[0].dep_name, S("LibB"));
    }

    arena_destroy(&alloc);
    return error;
}

int main(void) {
    init_temp_dir();

    int error = 0, this_error;

    T(test_fetch_single_dep);
    T(test_fetch_two_deps);
    T(test_fetch_no_deps);
    T(test_fetch_no_depend_path);
    T(test_fetch_download_failure);
    T(test_fetch_local_only_dep);
    T(test_fetch_lock_reuse);
    T(test_fetch_lock_outdated);
    T(test_fetch_archive_name_mismatch);
    T(test_fetch_transitive_dep);

    return error;
}
