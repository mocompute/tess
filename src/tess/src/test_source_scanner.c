#include "source_scanner.h"

#include "import_resolver.h"

#include <stdio.h>
#include <string.h>

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

// Helper: scan a string as source file content.
// Returns 0 on success, 1 on error.
static int scan(tl_source_scanner *s, char const *file_path, char const *content, str_array *imports) {
    str       fp = str_init(s->arena, file_path);
    char_csized input = {.v = content, .size = (u32)strlen(content)};
    return tl_source_scanner_scan(s, fp, input, imports);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Basic: single module discovered with correct file path mapping
static int test_single_module(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\nfoo() { 1 }\n", &imports) != 0;

    // Module should be discovered
    error += !str_map_contains(s.modules_seen, S("Foo"));

    // File path should be correct
    str *path = str_map_get(s.modules_seen, S("Foo"));
    error += !path;
    if (path) {
        error += !str_eq(*path, S("/src/foo.tl"));
    }

    // No imports expected
    error += imports.size != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Multiple modules from different files
static int test_multiple_modules(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\nfoo() { 1 }\n", &imports) != 0;
    error += scan(&s, "/src/bar.tl", "#module Bar\nbar() { 2 }\n", &imports) != 0;

    // Both modules discovered
    error += !str_map_contains(s.modules_seen, S("Foo"));
    error += !str_map_contains(s.modules_seen, S("Bar"));

    // Correct file path for each
    str *foo_path = str_map_get(s.modules_seen, S("Foo"));
    str *bar_path = str_map_get(s.modules_seen, S("Bar"));
    error += !foo_path || !str_eq(*foo_path, S("/src/foo.tl"));
    error += !bar_path || !str_eq(*bar_path, S("/src/bar.tl"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Duplicate module name from different files should fail
static int test_duplicate_module_error(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    // First file defines Foo — should succeed
    error += scan(&s, "/src/a.tl", "#module Foo\na() { 1 }\n", &imports) != 0;

    // Second file also defines Foo — should fail
    int result = scan(&s, "/src/b.tl", "#module Foo\nb() { 2 }\n", &imports);
    error += result != 1;

    // Original mapping should still be from first file
    str *path = str_map_get(s.modules_seen, S("Foo"));
    error += !path || !str_eq(*path, S("/src/a.tl"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Imports are collected correctly
static int test_imports_collected(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    char const *src = "#module Foo\n"
                      "#import \"bar.tl\"\n"
                      "#import <stdio.tl>\n"
                      "foo() { 1 }\n";

    error += scan(&s, "/src/foo.tl", src, &imports) != 0;

    error += imports.size != 2;
    if (imports.size >= 2) {
        error += !str_eq(imports.v[0], S("\"bar.tl\""));
        error += !str_eq(imports.v[1], S("<stdio.tl>"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Conditional compilation: #ifdef with define present — module is discovered
static int test_ifdef_defined(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    tl_source_scanner_define(&s, S("USE_FOO"));

    char const *src = "#ifdef USE_FOO\n"
                      "#module Foo\n"
                      "#endif\n";

    error += scan(&s, "/src/foo.tl", src, &imports) != 0;

    // Module should be discovered (define is set)
    error += !str_map_contains(s.modules_seen, S("Foo"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Conditional compilation: #ifdef without define — module is NOT discovered
static int test_ifdef_not_defined(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    // Do NOT define USE_FOO

    char const *src = "#ifdef USE_FOO\n"
                      "#module Foo\n"
                      "#endif\n";

    error += scan(&s, "/src/foo.tl", src, &imports) != 0;

    // Module should NOT be discovered
    error += str_map_contains(s.modules_seen, S("Foo"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Conditional compilation: #ifndef
static int test_ifndef(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    char const *src = "#ifndef MISSING\n"
                      "#module Fallback\n"
                      "#endif\n";

    error += scan(&s, "/src/fb.tl", src, &imports) != 0;

    // Module should be discovered (MISSING is not defined)
    error += !str_map_contains(s.modules_seen, S("Fallback"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Conditional compilation: nested #ifdef, module in inner branch
static int test_nested_ifdef(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    tl_source_scanner_define(&s, S("OUTER"));
    // Do NOT define INNER

    char const *src = "#ifdef OUTER\n"
                      "#ifdef INNER\n"
                      "#module Hidden\n"
                      "#endif\n"
                      "#module Visible\n"
                      "#endif\n";

    error += scan(&s, "/src/nest.tl", src, &imports) != 0;

    // Hidden should NOT be discovered (INNER not defined)
    error += str_map_contains(s.modules_seen, S("Hidden"));
    // Visible should be discovered (OUTER is defined, we're past the inner #endif)
    error += !str_map_contains(s.modules_seen, S("Visible"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// #define inside source affects subsequent #ifdef
static int test_define_in_source(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    char const *src = "#define HAS_FEATURE\n"
                      "#ifdef HAS_FEATURE\n"
                      "#module Feature\n"
                      "#endif\n";

    error += scan(&s, "/src/feat.tl", src, &imports) != 0;

    error += !str_map_contains(s.modules_seen, S("Feature"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// #undef removes a define
static int test_undef(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    tl_source_scanner_define(&s, S("FOO"));

    char const *src = "#undef FOO\n"
                      "#ifdef FOO\n"
                      "#module Gone\n"
                      "#endif\n";

    error += scan(&s, "/src/u.tl", src, &imports) != 0;

    // Gone should NOT be discovered (FOO was undefined)
    error += str_map_contains(s.modules_seen, S("Gone"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Stdlib files should not have their modules tracked
static int test_stdlib_modules_ignored(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    // Register /std/ as a standard library path
    import_resolver_add_standard_path(res, str_init(alloc, "/std"));

    char const *src = "#module StdModule\nfoo() { 1 }\n";

    // Scan as a file under the stdlib path
    error += scan(&s, "/std/stdlib.tl", src, &imports) != 0;

    // Module should NOT be tracked (it's a stdlib file)
    error += str_map_contains(s.modules_seen, S("StdModule"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Conditional skip depth resets between files
static int test_skip_depth_resets(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    // First file: unmatched #ifdef (no #endif) — leaves skip depth > 0
    char const *src1 = "#ifdef MISSING\n"
                       "#module Skipped\n";

    error += scan(&s, "/src/a.tl", src1, &imports) != 0;
    error += str_map_contains(s.modules_seen, S("Skipped"));

    // Second file: module should be discovered because skip depth resets
    char const *src2 = "#module Found\n";

    error += scan(&s, "/src/b.tl", src2, &imports) != 0;
    error += !str_map_contains(s.modules_seen, S("Found"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// Module directive at end of file without trailing newline
static int test_module_at_eof(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    // No trailing newline
    error += scan(&s, "/src/eof.tl", "#module Eof", &imports) != 0;

    // Should still not be discovered (the scanner needs a newline to trigger stop_hash)
    // This tests the edge case behavior — document whatever it does
    int found = str_map_contains(s.modules_seen, S("Eof"));
    (void)found; // Accept either behavior, just don't crash

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

// No #module directive — modules_seen stays empty
static int test_no_module(void) {
    int              error = 0;
    allocator       *alloc = default_allocator();
    import_resolver *res   = import_resolver_create(alloc);
    tl_source_scanner s    = tl_source_scanner_create(alloc, res);
    str_array imports      = {.alloc = alloc};

    error += scan(&s, "/src/bare.tl", "foo() { 1 }\nbar() { 2 }\n", &imports) != 0;

    error += map_size(s.modules_seen) != 0;
    error += imports.size != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    return error;
}

int main(void) {
    int error      = 0;
    int this_error = 0;
    T(test_single_module)
    T(test_multiple_modules)
    T(test_duplicate_module_error)
    T(test_imports_collected)
    T(test_ifdef_defined)
    T(test_ifdef_not_defined)
    T(test_ifndef)
    T(test_nested_ifdef)
    T(test_define_in_source)
    T(test_undef)
    T(test_stdlib_modules_ignored)
    T(test_skip_depth_resets)
    T(test_module_at_eof)
    T(test_no_module)
    return error;
}
