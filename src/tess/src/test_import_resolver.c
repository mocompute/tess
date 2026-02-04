#include "import_resolver.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <direct.h>
#include <io.h>
#define platform_mkdir(path) _mkdir(path)
#define NULL_DEVICE          "NUL"
#define dup                  _dup
#define dup2                 _dup2
#define fileno               _fileno
#define close                _close
#else
#include <sys/stat.h>
#include <unistd.h>
#define platform_mkdir(path) mkdir(path, 0755)
#define NULL_DEVICE          "/dev/null"
#endif

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

// Global test base directory (only needs to hold temp path like "/tmp/tess_import_test")
#define TEST_BASE_MAX 256
static char test_base[TEST_BASE_MAX];

// Initialize test base directory using platform temp path
static void init_test_base(void) {
#ifdef MOS_WINDOWS
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    snprintf(test_base, sizeof(test_base), "%stess_import_test", temp);
#else
    snprintf(test_base, sizeof(test_base), "/tmp/tess_import_test");
#endif
}

// Recursively create directories (like mkdir -p)
static int mkdirp(char const *path) {
    char   tmp[PLATFORM_PATH_MAX];
    char  *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // Remove trailing separator
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) tmp[len - 1] = 0;

    // Create each directory component
    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = 0;
            platform_mkdir(tmp);
#ifdef MOS_WINDOWS
            *p = '\\';
#else
            *p = '/';
#endif
        }
    }
    return platform_mkdir(tmp);
}

// Build a path by joining base and components
static void build_path(char *dest, size_t dest_size, char const *base, char const *suffix) {
    snprintf(dest, dest_size, "%s%c%s", base,
#ifdef MOS_WINDOWS
             '\\',
#else
             '/',
#endif
             suffix);
}

// Create a temporary test file
static int create_test_file(char const *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "#module test\n");
    fclose(f);
    return 0;
}

// Test that the same file imported via different paths is detected as duplicate
static int test_duplicate_different_paths(void) {
    allocator *alloc = default_allocator();

    // Create test directory structure:
    // test_base/dup1/
    // test_base/dup1/lib/
    // test_base/dup1/lib/helper.tl
    char dir_path[PLATFORM_PATH_MAX];
    char file_path[PLATFORM_PATH_MAX];
    char base_path[PLATFORM_PATH_MAX];
    char lib_path[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "dup1/lib");
    mkdirp(dir_path);

    build_path(file_path, sizeof(file_path), test_base, "dup1/lib/helper.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);

    // Add two paths that can reach the same file
    build_path(base_path, sizeof(base_path), test_base, "dup1");
    build_path(lib_path, sizeof(lib_path), test_base, "dup1/lib");
    import_resolver_add_user_path(resolver, str_init_static(base_path));
    import_resolver_add_user_path(resolver, str_init_static(lib_path));

    // First import: "lib/helper.tl" from base_path
    import_result r1 = import_resolver_resolve(resolver, S("\"lib/helper.tl\""), str_empty());
    if (str_is_empty(r1.canonical_path)) {
        fprintf(stderr, "  first resolve failed\n");
        return 1;
    }
    if (r1.is_duplicate) {
        fprintf(stderr, "  first import should not be duplicate\n");
        return 1;
    }

    // Mark it as imported
    import_resolver_mark_imported(resolver, r1.canonical_path);

    // Second import: "helper.tl" from lib_path (same file, different path)
    import_result r2 = import_resolver_resolve(resolver, S("\"helper.tl\""), str_empty());
    if (str_is_empty(r2.canonical_path)) {
        fprintf(stderr, "  second resolve failed\n");
        return 1;
    }
    if (!r2.is_duplicate) {
        fprintf(stderr, "  second import should be duplicate\n");
        fprintf(stderr, "  path1: %s\n", str_cstr(&r1.canonical_path));
        fprintf(stderr, "  path2: %s\n", str_cstr(&r2.canonical_path));
        return 1;
    }

    // Verify canonical paths are the same
    if (!str_eq(r1.canonical_path, r2.canonical_path)) {
        fprintf(stderr, "  canonical paths should match\n");
        fprintf(stderr, "  path1: %s\n", str_cstr(&r1.canonical_path));
        fprintf(stderr, "  path2: %s\n", str_cstr(&r2.canonical_path));
        return 1;
    }

    return 0;
}

// Test duplicate detection with relative paths containing ..
static int test_duplicate_with_dotdot(void) {
    allocator *alloc = default_allocator();

    // Create test directory structure:
    // test_base/dup2/
    // test_base/dup2/src/
    // test_base/dup2/src/main.tl
    // test_base/dup2/lib/
    // test_base/dup2/lib/helper.tl
    char src_dir[PLATFORM_PATH_MAX];
    char lib_dir[PLATFORM_PATH_MAX];
    char file_path[PLATFORM_PATH_MAX];
    char base_path[PLATFORM_PATH_MAX];
    char main_path[PLATFORM_PATH_MAX];

    build_path(src_dir, sizeof(src_dir), test_base, "dup2/src");
    build_path(lib_dir, sizeof(lib_dir), test_base, "dup2/lib");
    mkdirp(src_dir);
    mkdirp(lib_dir);

    build_path(file_path, sizeof(file_path), test_base, "dup2/lib/helper.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    build_path(base_path, sizeof(base_path), test_base, "dup2");
    import_resolver_add_user_path(resolver, str_init_static(base_path));

    // First import: "lib/helper.tl" (direct path)
    import_result r1 = import_resolver_resolve(resolver, S("\"lib/helper.tl\""), str_empty());
    if (str_is_empty(r1.canonical_path)) {
        fprintf(stderr, "  first resolve failed\n");
        return 1;
    }
    import_resolver_mark_imported(resolver, r1.canonical_path);

    // Second import: "../lib/helper.tl" relative to src/main.tl
    build_path(main_path, sizeof(main_path), test_base, "dup2/src/main.tl");
    import_result r2 =
      import_resolver_resolve(resolver, S("\"../lib/helper.tl\""), str_init_static(main_path));
    if (str_is_empty(r2.canonical_path)) {
        fprintf(stderr, "  second resolve failed\n");
        return 1;
    }
    if (!r2.is_duplicate) {
        fprintf(stderr, "  second import should be duplicate\n");
        fprintf(stderr, "  path1: %s\n", str_cstr(&r1.canonical_path));
        fprintf(stderr, "  path2: %s\n", str_cstr(&r2.canonical_path));
        return 1;
    }

    return 0;
}

// Test that different files are not marked as duplicates
static int test_not_duplicate(void) {
    allocator *alloc = default_allocator();

    char       dir_path[PLATFORM_PATH_MAX];
    char       file_a[PLATFORM_PATH_MAX];
    char       file_b[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "nodup");
    mkdirp(dir_path);

    build_path(file_a, sizeof(file_a), test_base, "nodup/a.tl");
    build_path(file_b, sizeof(file_b), test_base, "nodup/b.tl");

    if (create_test_file(file_a)) {
        fprintf(stderr, "  failed to create test file a.tl\n");
        return 1;
    }
    if (create_test_file(file_b)) {
        fprintf(stderr, "  failed to create test file b.tl\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    import_resolver_add_user_path(resolver, str_init_static(dir_path));

    import_result r1 = import_resolver_resolve(resolver, S("\"a.tl\""), str_empty());
    if (str_is_empty(r1.canonical_path) || r1.is_duplicate) {
        fprintf(stderr, "  first resolve failed or unexpected duplicate\n");
        return 1;
    }
    import_resolver_mark_imported(resolver, r1.canonical_path);

    import_result r2 = import_resolver_resolve(resolver, S("\"b.tl\""), str_empty());
    if (str_is_empty(r2.canonical_path)) {
        fprintf(stderr, "  second resolve failed\n");
        return 1;
    }
    if (r2.is_duplicate) {
        fprintf(stderr, "  different files should not be duplicates\n");
        return 1;
    }

    return 0;
}

// Test that quoted imports do NOT search standard paths
static int test_quoted_ignores_standard_paths(void) {
    allocator *alloc = default_allocator();

    // Create a file ONLY in standard paths, not in user paths
    char dir_path[PLATFORM_PATH_MAX];
    char file_path[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "std_only");
    mkdirp(dir_path);

    build_path(file_path, sizeof(file_path), test_base, "std_only/std_only.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    // Add as standard path only, NOT as user path
    import_resolver_add_standard_path(resolver, str_init_static(dir_path));

    // Quoted import should NOT find it (should fail)
    import_result r = import_resolver_resolve(resolver, S("\"std_only.tl\""), str_empty());

    if (!str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  quoted import should NOT search standard paths\n");
        fprintf(stderr, "  unexpectedly found: %s\n", str_cstr(&r.canonical_path));
        return 1;
    }

    return 0;
}

// Test that angle bracket imports DO search standard paths
static int test_angle_bracket_finds_standard_paths(void) {
    allocator *alloc = default_allocator();

    char       dir_path[PLATFORM_PATH_MAX];
    char       file_path[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "std_find");
    mkdirp(dir_path);

    build_path(file_path, sizeof(file_path), test_base, "std_find/stdlib.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    import_resolver_add_standard_path(resolver, str_init_static(dir_path));

    // Angle bracket import should find it
    import_result r = import_resolver_resolve(resolver, S("<stdlib.tl>"), str_empty());
    if (str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  angle bracket import should find file in standard paths\n");
        return 1;
    }

    return 0;
}

// Test that angle bracket imports do NOT search user paths (-I)
static int test_angle_bracket_ignores_user_paths(void) {
    allocator *alloc = default_allocator();

    // Create a file ONLY in user paths, not in standard paths
    char dir_path[PLATFORM_PATH_MAX];
    char file_path[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "user_only");
    mkdirp(dir_path);

    build_path(file_path, sizeof(file_path), test_base, "user_only/user_only.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    // Add as user path only, NOT as standard path
    import_resolver_add_user_path(resolver, str_init_static(dir_path));

    // Angle bracket import should NOT find it
    import_result r = import_resolver_resolve(resolver, S("<user_only.tl>"), str_empty());

    if (!str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  angle bracket import should NOT search user paths\n");
        fprintf(stderr, "  unexpectedly found: %s\n", str_cstr(&r.canonical_path));
        return 1;
    }

    return 0;
}

// Test that angle bracket imports do NOT search relative to importing file
static int test_angle_bracket_ignores_relative(void) {
    allocator *alloc = default_allocator();

    // Create a file next to the "importing" file
    char dir_path[PLATFORM_PATH_MAX];
    char file_path[PLATFORM_PATH_MAX];
    char main_path[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "rel_ignore/src");
    mkdirp(dir_path);

    build_path(file_path, sizeof(file_path), test_base, "rel_ignore/src/sibling.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    // No paths added - only relative would work for quoted

    // Angle bracket import should NOT find the sibling file
    build_path(main_path, sizeof(main_path), test_base, "rel_ignore/src/main.tl");
    import_result r = import_resolver_resolve(resolver, S("<sibling.tl>"), str_init_static(main_path));

    if (!str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  angle bracket import should NOT search relative paths\n");
        fprintf(stderr, "  unexpectedly found: %s\n", str_cstr(&r.canonical_path));
        return 1;
    }

    return 0;
}

// Test that quoted imports search relative to importing file FIRST, before -I paths
static int test_quoted_relative_precedence(void) {
    allocator *alloc = default_allocator();

    // Create two files with the same name in different locations
    // One relative to importing file, one in -I path
    char src_dir[PLATFORM_PATH_MAX];
    char lib_dir[PLATFORM_PATH_MAX];
    char lib_file[PLATFORM_PATH_MAX];
    char src_file[PLATFORM_PATH_MAX];
    char main_path[PLATFORM_PATH_MAX];

    build_path(src_dir, sizeof(src_dir), test_base, "prec/src");
    build_path(lib_dir, sizeof(lib_dir), test_base, "prec/lib");
    mkdirp(src_dir);
    mkdirp(lib_dir);

    // Create file in -I path with specific content marker
    build_path(lib_file, sizeof(lib_file), test_base, "prec/lib/common.tl");
    FILE *f1 = fopen(lib_file, "w");
    if (!f1) {
        fprintf(stderr, "  failed to create lib file\n");
        return 1;
    }
    fprintf(f1, "#module common_lib\n");
    fclose(f1);

    // Create file relative to importing file with different content marker
    build_path(src_file, sizeof(src_file), test_base, "prec/src/common.tl");
    FILE *f2 = fopen(src_file, "w");
    if (!f2) {
        fprintf(stderr, "  failed to create src file\n");
        return 1;
    }
    fprintf(f2, "#module common_src\n");
    fclose(f2);

    import_resolver *resolver = import_resolver_create(alloc);
    import_resolver_add_user_path(resolver, str_init_static(lib_dir));

    // Resolve "common.tl" from src/main.tl
    // Should find src/common.tl (relative), NOT lib/common.tl (-I path)
    build_path(main_path, sizeof(main_path), test_base, "prec/src/main.tl");
    import_result r = import_resolver_resolve(resolver, S("\"common.tl\""), str_init_static(main_path));

    if (str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  resolve failed\n");
        return 1;
    }

    // The path should be the one relative to importing file
    char const *path = str_cstr(&r.canonical_path);
    // Check for /src/common.tl or \src\common.tl
    if (strstr(path, "/src/common.tl") == NULL && strstr(path, "\\src\\common.tl") == NULL) {
        fprintf(stderr, "  should find relative file, not -I path file\n");
        fprintf(stderr, "  found: %s\n", path);
        fprintf(stderr, "  expected path containing: /src/common.tl or \\src\\common.tl\n");
        return 1;
    }

    return 0;
}

// Test that quoted imports fall back to -I paths when relative not found
static int test_quoted_fallback_to_user_paths(void) {
    allocator *alloc = default_allocator();

    // Create a file ONLY in -I path, not relative to importing file
    char src_dir[PLATFORM_PATH_MAX];
    char lib_dir[PLATFORM_PATH_MAX];
    char file_path[PLATFORM_PATH_MAX];
    char main_path[PLATFORM_PATH_MAX];

    build_path(src_dir, sizeof(src_dir), test_base, "fallback/src");
    build_path(lib_dir, sizeof(lib_dir), test_base, "fallback/lib");
    mkdirp(src_dir);
    mkdirp(lib_dir);

    build_path(file_path, sizeof(file_path), test_base, "fallback/lib/libonly.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    import_resolver_add_user_path(resolver, str_init_static(lib_dir));

    // Resolve "libonly.tl" from src/main.tl
    // Not found relative, should fall back to -I path
    build_path(main_path, sizeof(main_path), test_base, "fallback/src/main.tl");
    import_result r = import_resolver_resolve(resolver, S("\"libonly.tl\""), str_init_static(main_path));

    if (str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  quoted import should fall back to -I paths\n");
        return 1;
    }

    char const *path = str_cstr(&r.canonical_path);
    // Check for /lib/libonly.tl or \lib\libonly.tl
    if (strstr(path, "/lib/libonly.tl") == NULL && strstr(path, "\\lib\\libonly.tl") == NULL) {
        fprintf(stderr, "  should find file in -I path\n");
        fprintf(stderr, "  found: %s\n", path);
        return 1;
    }

    return 0;
}

// Test cycle detection
static int test_cycle_detection(void) {
    allocator *alloc = default_allocator();

    char       dir_path[PLATFORM_PATH_MAX];
    char       file_path[PLATFORM_PATH_MAX];

    build_path(dir_path, sizeof(dir_path), test_base, "cycle");
    mkdirp(dir_path);

    build_path(file_path, sizeof(file_path), test_base, "cycle/cycle.tl");
    if (create_test_file(file_path)) {
        fprintf(stderr, "  failed to create test file\n");
        return 1;
    }

    import_resolver *resolver = import_resolver_create(alloc);
    import_resolver_add_user_path(resolver, str_init_static(dir_path));

    import_result r = import_resolver_resolve(resolver, S("\"cycle.tl\""), str_empty());
    if (str_is_empty(r.canonical_path)) {
        fprintf(stderr, "  resolve failed\n");
        return 1;
    }

    // Begin import - should succeed
    if (import_resolver_begin_import(resolver, r.canonical_path)) {
        fprintf(stderr, "  begin_import should succeed first time\n");
        return 1;
    }

    // Begin same import again - should detect cycle
    if (!import_resolver_begin_import(resolver, r.canonical_path)) {
        fprintf(stderr, "  begin_import should detect cycle\n");
        return 1;
    }

    // End import
    import_resolver_end_import(resolver, r.canonical_path);

    // Begin again - should succeed now
    if (import_resolver_begin_import(resolver, r.canonical_path)) {
        fprintf(stderr, "  begin_import should succeed after end_import\n");
        return 1;
    }

    return 0;
}

int main(void) {
    init_test_base();

    int error      = 0;
    int this_error = 0;
    T(test_duplicate_different_paths)
    T(test_duplicate_with_dotdot)
    T(test_not_duplicate)
    T(test_cycle_detection)
    T(test_quoted_ignores_standard_paths)
    T(test_angle_bracket_finds_standard_paths)
    T(test_angle_bracket_ignores_user_paths)
    T(test_angle_bracket_ignores_relative)
    T(test_quoted_relative_precedence)
    T(test_quoted_fallback_to_user_paths)
    return error;
}
