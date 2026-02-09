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
    str         fp    = str_init(s->arena, file_path);
    char_csized input = {.v = content, .size = (u32)strlen(content)};
    return tl_source_scanner_scan(s, fp, input, imports);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Basic: single module discovered with correct file path mapping
static int test_single_module(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

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
    arena_destroy(&alloc);
    return error;
}

// Multiple modules from different files
static int test_multiple_modules(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

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
    arena_destroy(&alloc);
    return error;
}

// Duplicate module name from different files should succeed (compiler accepts it)
static int test_duplicate_module_allowed(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    // First file defines Foo — should succeed
    error += scan(&s, "/src/a.tl", "#module Foo\na() { 1 }\n", &imports) != 0;

    // Second file also defines Foo — should also succeed
    error += scan(&s, "/src/b.tl", "#module Foo\nb() { 2 }\n", &imports) != 0;

    // Mapping should be updated to second file
    str *path = str_map_get(s.modules_seen, S("Foo"));
    error += !path || !str_eq(*path, S("/src/b.tl"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Imports are collected correctly
static int test_imports_collected(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#module Foo\n"
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
    arena_destroy(&alloc);
    return error;
}

// Conditional compilation: #ifdef with define present — module is discovered
static int test_ifdef_defined(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    tl_source_scanner_define(&s, S("USE_FOO"));

    char const *src = "#ifdef USE_FOO\n"
                      "#module Foo\n"
                      "#endif\n";

    error += scan(&s, "/src/foo.tl", src, &imports) != 0;

    // Module should be discovered (define is set)
    error += !str_map_contains(s.modules_seen, S("Foo"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Conditional compilation: #ifdef without define — module is NOT discovered
static int test_ifdef_not_defined(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    // Do NOT define USE_FOO

    char const *src = "#ifdef USE_FOO\n"
                      "#module Foo\n"
                      "#endif\n";

    error += scan(&s, "/src/foo.tl", src, &imports) != 0;

    // Module should NOT be discovered
    error += str_map_contains(s.modules_seen, S("Foo"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Conditional compilation: #ifndef
static int test_ifndef(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#ifndef MISSING\n"
                                "#module Fallback\n"
                                "#endif\n";

    error += scan(&s, "/src/fb.tl", src, &imports) != 0;

    // Module should be discovered (MISSING is not defined)
    error += !str_map_contains(s.modules_seen, S("Fallback"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Conditional compilation: nested #ifdef, module in inner branch
static int test_nested_ifdef(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

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
    arena_destroy(&alloc);
    return error;
}

// #define inside source affects subsequent #ifdef
static int test_define_in_source(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#define HAS_FEATURE\n"
                                "#ifdef HAS_FEATURE\n"
                                "#module Feature\n"
                                "#endif\n";

    error += scan(&s, "/src/feat.tl", src, &imports) != 0;

    error += !str_map_contains(s.modules_seen, S("Feature"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// #undef removes a define
static int test_undef(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    tl_source_scanner_define(&s, S("FOO"));

    char const *src = "#undef FOO\n"
                      "#ifdef FOO\n"
                      "#module Gone\n"
                      "#endif\n";

    error += scan(&s, "/src/u.tl", src, &imports) != 0;

    // Gone should NOT be discovered (FOO was undefined)
    error += str_map_contains(s.modules_seen, S("Gone"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Stdlib files should not have their modules tracked
static int test_stdlib_modules_ignored(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    // Register /std/ as a standard library path
    import_resolver_add_standard_path(res, str_init(alloc, "/std"));

    char const *src = "#module StdModule\nfoo() { 1 }\n";

    // Scan as a file under the stdlib path
    error += scan(&s, "/std/stdlib.tl", src, &imports) != 0;

    // Module should NOT be tracked (it's a stdlib file)
    error += str_map_contains(s.modules_seen, S("StdModule"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Conditional skip depth resets between files
static int test_skip_depth_resets(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

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
    arena_destroy(&alloc);
    return error;
}

// Module directive at end of file without trailing newline
static int test_module_at_eof(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    // No trailing newline
    error += scan(&s, "/src/eof.tl", "#module Eof", &imports) != 0;

    // Should still not be discovered (the scanner needs a newline to trigger stop_hash)
    // This tests the edge case behavior — document whatever it does
    int found = str_map_contains(s.modules_seen, S("Eof"));
    (void)found; // Accept either behavior, just don't crash

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// No #module directive — modules_seen stays empty
static int test_no_module(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    error += scan(&s, "/src/bare.tl", "foo() { 1 }\nbar() { 2 }\n", &imports) != 0;

    error += map_size(s.modules_seen) != 0;
    error += imports.size != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Multiline string containing #module should be ignored
static int test_string_hides_directive(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#module Real\n"
                                "x = \"\n"
                                "#module Fake\n"
                                "\"\n";

    error += scan(&s, "/src/str.tl", src, &imports) != 0;

    // Only Real should be discovered, not Fake
    error += !str_map_contains(s.modules_seen, S("Real"));
    error += str_map_contains(s.modules_seen, S("Fake"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// String with escaped quote should not prematurely end string tracking
static int test_string_escaped_quote(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#module Real\n"
                                "x = \"foo\\\"\n"
                                "#module Fake\n"
                                "\"\n";

    error += scan(&s, "/src/esc.tl", src, &imports) != 0;

    // Fake is inside the string (the \" doesn't close it)
    error += !str_map_contains(s.modules_seen, S("Real"));
    error += str_map_contains(s.modules_seen, S("Fake"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Single-line string with # inside should not be treated as directive
static int test_string_single_line(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    // The string is on the same line as other content — noise state handles it.
    // But test the case where a string starts at a newline boundary.
    char const *src = "#module Real\n"
                      "x = \"#module Fake\"\n";

    error += scan(&s, "/src/sl.tl", src, &imports) != 0;

    error += !str_map_contains(s.modules_seen, S("Real"));
    error += str_map_contains(s.modules_seen, S("Fake"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Line comment should not hide next line's directive
static int test_comment_does_not_span_lines(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "// this is a comment\n"
                                "#module Real\n";

    error += scan(&s, "/src/cmt.tl", src, &imports) != 0;

    error += !str_map_contains(s.modules_seen, S("Real"));

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Comment containing directive-like text should be ignored
static int test_comment_hides_directive(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#module Real\n"
                                "// #import \"fake.tl\"\n"
                                "foo() { 1 }\n";

    error += scan(&s, "/src/ci.tl", src, &imports) != 0;

    error += !str_map_contains(s.modules_seen, S("Real"));
    // The commented-out import should NOT be collected
    error += imports.size != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Unterminated string hides all subsequent directives until EOF
static int test_string_unterminated(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    char const       *src     = "#module Real\n"
                                "x = \"unterminated\n"
                                "#module Fake\n"
                                "#import \"hidden.tl\"\n";

    error += scan(&s, "/src/ut.tl", src, &imports) != 0;

    // Real is before the string, should be discovered
    error += !str_map_contains(s.modules_seen, S("Real"));
    // Fake is inside the unterminated string, should NOT be discovered
    error += str_map_contains(s.modules_seen, S("Fake"));
    // Import is also inside the string
    error += imports.size != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// --- collect_imports tests (no conditional compilation, string/comment aware) ---

// Helper: collect imports from a string without a full scanner.
static str_array collect(allocator *alloc, char const *content) {
    str_array   imports = {.alloc = alloc};
    char_csized input   = {.v = content, .size = (u32)strlen(content)};
    tl_source_scanner_collect_imports(alloc, input, &imports);
    return imports;
}

// Extracts both quoted and angle-bracket imports
static int test_collect_basic(void) {
    int        error   = 0;
    allocator *alloc   = arena_create(default_allocator(), 1024);
    str_array  imports = collect(alloc, "#import \"foo.tl\"\n#import <stdio.tl>\n");

    error += imports.size != 2;
    if (imports.size >= 2) {
        error += !str_eq(imports.v[0], S("\"foo.tl\""));
        error += !str_eq(imports.v[1], S("<stdio.tl>"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Ignores #import inside a string literal
static int test_collect_ignores_string(void) {
    int        error   = 0;
    allocator *alloc   = arena_create(default_allocator(), 1024);
    str_array  imports = collect(alloc, "x = \"\n#import \"fake.tl\"\n\"\n#import \"real.tl\"\n");

    error += imports.size != 1;
    if (imports.size >= 1) {
        error += !str_eq(imports.v[0], S("\"real.tl\""));
    }

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Ignores #import inside a comment
static int test_collect_ignores_comment(void) {
    int        error   = 0;
    allocator *alloc   = arena_create(default_allocator(), 1024);
    str_array  imports = collect(alloc, "// #import \"fake.tl\"\n#import \"real.tl\"\n");

    error += imports.size != 1;
    if (imports.size >= 1) {
        error += !str_eq(imports.v[0], S("\"real.tl\""));
    }

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Collects imports from inside #ifdef blocks (no conditional filtering)
static int test_collect_ignores_conditionals(void) {
    int        error   = 0;
    allocator *alloc   = arena_create(default_allocator(), 1024);
    str_array  imports = collect(alloc, "#ifdef FEATURE\n"
                                         "#import \"conditional.tl\"\n"
                                         "#endif\n"
                                         "#import \"always.tl\"\n");

    // Both imports should be collected (no conditional filtering)
    error += imports.size != 2;
    if (imports.size >= 2) {
        error += !str_eq(imports.v[0], S("\"conditional.tl\""));
        error += !str_eq(imports.v[1], S("\"always.tl\""));
    }

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// --- Validation tests ---

// Manifest declares module not found in source -> error
static int test_validate_missing_module(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\nfoo() { 1 }\n", &imports) != 0;

    str                               manifest_modules[] = {S("Bar")};
    tl_source_scanner_validate_result result = tl_source_scanner_validate(&s, manifest_modules, 1, 0);

    error += result.error_count != 1;
    error += result.warning_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Manifest module found with [[export]] -> no errors, no warnings
static int test_validate_ok(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\n[[export]] foo() { 1 }\n", &imports) != 0;

    str                               manifest_modules[] = {S("Foo")};
    tl_source_scanner_validate_result result = tl_source_scanner_validate(&s, manifest_modules, 1, 0);

    error += result.error_count != 0;
    error += result.warning_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Empty manifest modules list -> skip validation entirely
static int test_validate_empty_modules(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\nfoo() { 1 }\n", &imports) != 0;

    tl_source_scanner_validate_result result = tl_source_scanner_validate(&s, null, 0, 0);

    error += result.error_count != 0;
    error += result.warning_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Internal module (in source, not in manifest, no exports) is fine
static int test_validate_internal_module_ok(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\n[[export]] foo() { 1 }\n", &imports) != 0;
    error += scan(&s, "/src/internal.tl", "#module Foo.Internal\ninternal() { 2 }\n", &imports) != 0;

    str                               manifest_modules[] = {S("Foo")};
    tl_source_scanner_validate_result result = tl_source_scanner_validate(&s, manifest_modules, 1, 0);

    error += result.error_count != 0;
    error += result.warning_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

// Multiple manifest modules, one missing -> error (warnings skipped)
static int test_validate_multiple_one_missing(void) {
    int               error   = 0;
    allocator        *alloc   = arena_create(default_allocator(), 1024);
    import_resolver  *res     = import_resolver_create(alloc);
    tl_source_scanner s       = tl_source_scanner_create(alloc, res);
    str_array         imports = {.alloc = alloc};

    error += scan(&s, "/src/foo.tl", "#module Foo\n[[export]] foo() { 1 }\n", &imports) != 0;

    str                               manifest_modules[] = {S("Foo"), S("Bar")};
    tl_source_scanner_validate_result result = tl_source_scanner_validate(&s, manifest_modules, 2, 0);

    error += result.error_count != 1;
    error += result.warning_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed\n", error);
    arena_destroy(&alloc);
    return error;
}

int main(void) {
    int error      = 0;
    int this_error = 0;
    T(test_single_module)
    T(test_multiple_modules)
    T(test_duplicate_module_allowed)
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
    T(test_string_hides_directive)
    T(test_string_escaped_quote)
    T(test_string_single_line)
    T(test_comment_does_not_span_lines)
    T(test_comment_hides_directive)
    T(test_string_unterminated)

    T(test_collect_basic)
    T(test_collect_ignores_string)
    T(test_collect_ignores_comment)
    T(test_collect_ignores_conditionals)
    T(test_validate_missing_module)
    T(test_validate_ok)
    T(test_validate_empty_modules)
    T(test_validate_internal_module_ok)
    T(test_validate_multiple_one_missing)
    return error;
}
