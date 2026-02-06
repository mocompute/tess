#include "manifest.h"

#include <stdio.h>
#include <string.h>

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

// Helper: parse a string literal as manifest
static int parse(allocator *alloc, char const *text, tl_manifest *out) {
    return tl_manifest_parse(alloc, text, (u32)strlen(text), out);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static int test_basic_package(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = MathUtils\n"
                        "version = 1.0.0\n"
                        "author = Alice\n"
                        "modules = [MathUtils, MathUtils.Internal]\n"
                        "lib_path = [libs/, vendor/]\n";

    error += parse(alloc, input, &m) != 0;
    error += !str_eq(m.package.name, S("MathUtils"));
    error += !str_eq(m.package.version, S("1.0.0"));
    error += !str_eq(m.package.author, S("Alice"));
    error += m.package.module_count != 2;
    if (m.package.module_count == 2) {
        error += !str_eq(m.package.modules[0], S("MathUtils"));
        error += !str_eq(m.package.modules[1], S("MathUtils.Internal"));
    }
    error += m.package.lib_path_count != 2;
    if (m.package.lib_path_count == 2) {
        error += !str_eq(m.package.lib_path[0], S("libs/"));
        error += !str_eq(m.package.lib_path[1], S("vendor/"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_basic_package\n", error);
    return error;
}

static int test_minimal_package(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n"
                        "version = 0.1.0\n";

    error += parse(alloc, input, &m) != 0;
    error += !str_eq(m.package.name, S("Foo"));
    error += !str_eq(m.package.version, S("0.1.0"));
    error += !str_is_empty(m.package.author);
    error += m.package.module_count != 0;
    error += m.package.lib_path_count != 0;
    error += m.dep_count != 0;
    error += m.optional_dep_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed in test_minimal_package\n", error);
    return error;
}

static int test_comments_and_blanks(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "# This is a comment\n"
                        "\n"
                        "[package]\n"
                        "# Another comment\n"
                        "name = Foo\n"
                        "\n"
                        "version = 1.0.0\n"
                        "# trailing comment\n";

    error += parse(alloc, input, &m) != 0;
    error += !str_eq(m.package.name, S("Foo"));
    error += !str_eq(m.package.version, S("1.0.0"));

    if (error) fprintf(stderr, "  %d check(s) failed in test_comments_and_blanks\n", error);
    return error;
}

static int test_dependencies(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = MyApp\n"
                        "version = 0.1.0\n"
                        "\n"
                        "[depend.LoggingLib]\n"
                        "version = 2.0.0\n"
                        "path = libs/LoggingLib.tlib\n";

    error += parse(alloc, input, &m) != 0;
    error += m.dep_count != 1;
    if (m.dep_count == 1) {
        error += !str_eq(m.deps[0].name, S("LoggingLib"));
        error += !str_eq(m.deps[0].version, S("2.0.0"));
        error += !str_eq(m.deps[0].path, S("libs/LoggingLib.tlib"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_dependencies\n", error);
    return error;
}

static int test_optional_dependencies(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = MyLib\n"
                        "version = 1.0.0\n"
                        "\n"
                        "[depend-optional.WinAPI]\n"
                        "version = 3.0.0\n"
                        "path = libs/WinAPI.tlib\n";

    error += parse(alloc, input, &m) != 0;
    error += m.dep_count != 0;
    error += m.optional_dep_count != 1;
    if (m.optional_dep_count == 1) {
        error += !str_eq(m.optional_deps[0].name, S("WinAPI"));
        error += !str_eq(m.optional_deps[0].version, S("3.0.0"));
        error += !str_eq(m.optional_deps[0].path, S("libs/WinAPI.tlib"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_optional_dependencies\n", error);
    return error;
}

static int test_multiple_deps(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = MyApp\n"
                        "version = 0.1.0\n"
                        "\n"
                        "[depend.LoggingLib]\n"
                        "version = 2.0.0\n"
                        "\n"
                        "[depend.MathUtils]\n"
                        "version = 1.0.0\n"
                        "path = libs/MathUtils.tlib\n";

    error += parse(alloc, input, &m) != 0;
    error += m.dep_count != 2;
    if (m.dep_count == 2) {
        error += !str_eq(m.deps[0].name, S("LoggingLib"));
        error += !str_eq(m.deps[0].version, S("2.0.0"));
        error += !str_is_empty(m.deps[0].path);
        error += !str_eq(m.deps[1].name, S("MathUtils"));
        error += !str_eq(m.deps[1].version, S("1.0.0"));
        error += !str_eq(m.deps[1].path, S("libs/MathUtils.tlib"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_multiple_deps\n", error);
    return error;
}

static int test_missing_name(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "version = 1.0.0\n";

    // Should fail: name is required
    error += parse(alloc, input, &m) != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_name\n", error);
    return error;
}

static int test_missing_version(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n";

    // Should fail: version is required
    error += parse(alloc, input, &m) != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_version\n", error);
    return error;
}

static int test_missing_dep_version(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n"
                        "version = 1.0.0\n"
                        "[depend.Bar]\n"
                        "path = libs/Bar.tlib\n";

    // Should fail: depend version is required
    error += parse(alloc, input, &m) != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_missing_dep_version\n", error);
    return error;
}

static int test_quotes_rejected(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = \"Foo\"\n"
                        "version = 1.0.0\n";

    // Should fail: quotes not allowed
    error += parse(alloc, input, &m) != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_quotes_rejected\n", error);
    return error;
}

static int test_array_spaces_rejected(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n"
                        "version = 1.0.0\n"
                        "modules = [Foo Bar]\n";

    // Should fail: spaces in array elements
    error += parse(alloc, input, &m) != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_array_spaces_rejected\n", error);
    return error;
}

static int test_array_trailing_text_rejected(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n"
                        "version = 1.0.0\n"
                        "modules = [A, B] garbage\n";

    // Should fail: text after closing bracket
    error += parse(alloc, input, &m) != 1;

    if (error) fprintf(stderr, "  %d check(s) failed in test_array_trailing_text_rejected\n", error);
    return error;
}

static int test_empty_array(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n"
                        "version = 1.0.0\n"
                        "modules = []\n";

    error += parse(alloc, input, &m) != 0;
    error += m.package.module_count != 0;
    error += m.package.modules != 0;

    if (error) fprintf(stderr, "  %d check(s) failed in test_empty_array\n", error);
    return error;
}

static int test_whitespace_trimming(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "  name   =   Foo  \n"
                        "  version  =  1.0.0  \n"
                        "  modules  =  [ A , B , C ]  \n";

    error += parse(alloc, input, &m) != 0;
    error += !str_eq(m.package.name, S("Foo"));
    error += !str_eq(m.package.version, S("1.0.0"));
    error += m.package.module_count != 3;
    if (m.package.module_count == 3) {
        error += !str_eq(m.package.modules[0], S("A"));
        error += !str_eq(m.package.modules[1], S("B"));
        error += !str_eq(m.package.modules[2], S("C"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_whitespace_trimming\n", error);
    return error;
}

static int test_multiline_array(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = Foo\n"
                        "version = 1.0.0\n"
                        "modules = [\n"
                        "  Alpha,\n"
                        "  Beta,\n"
                        "  Gamma\n"
                        "]\n"
                        "lib_path = [libs/,\n"
                        "  vendor/]\n";

    error += parse(alloc, input, &m) != 0;
    error += m.package.module_count != 3;
    if (m.package.module_count == 3) {
        error += !str_eq(m.package.modules[0], S("Alpha"));
        error += !str_eq(m.package.modules[1], S("Beta"));
        error += !str_eq(m.package.modules[2], S("Gamma"));
    }
    error += m.package.lib_path_count != 2;
    if (m.package.lib_path_count == 2) {
        error += !str_eq(m.package.lib_path[0], S("libs/"));
        error += !str_eq(m.package.lib_path[1], S("vendor/"));
    }

    if (error) fprintf(stderr, "  %d check(s) failed in test_multiline_array\n", error);
    return error;
}

static int test_full_manifest(void) {
    int         error = 0;
    allocator  *alloc = default_allocator();
    tl_manifest m;

    char const *input = "[package]\n"
                        "name = MathUtils\n"
                        "version = 1.0.0\n"
                        "author = Alice\n"
                        "modules = [MathUtils]\n"
                        "lib_path = [libs/]\n"
                        "\n"
                        "# Dependencies use [depend.Name] sections\n"
                        "[depend.LoggingLib]\n"
                        "version = 2.0.0\n"
                        "path = libs/LoggingLib.tlib\n";

    error += parse(alloc, input, &m) != 0;
    error += !str_eq(m.package.name, S("MathUtils"));
    error += !str_eq(m.package.version, S("1.0.0"));
    error += !str_eq(m.package.author, S("Alice"));
    error += m.package.module_count != 1;
    if (m.package.module_count == 1) {
        error += !str_eq(m.package.modules[0], S("MathUtils"));
    }
    error += m.package.lib_path_count != 1;
    if (m.package.lib_path_count == 1) {
        error += !str_eq(m.package.lib_path[0], S("libs/"));
    }
    error += m.dep_count != 1;
    if (m.dep_count == 1) {
        error += !str_eq(m.deps[0].name, S("LoggingLib"));
        error += !str_eq(m.deps[0].version, S("2.0.0"));
        error += !str_eq(m.deps[0].path, S("libs/LoggingLib.tlib"));
    }
    error += m.optional_dep_count != 0;

    if (error) fprintf(stderr, "  %d check(s) failed in test_full_manifest\n", error);
    return error;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_basic_package)
    T(test_minimal_package)
    T(test_comments_and_blanks)
    T(test_dependencies)
    T(test_optional_dependencies)
    T(test_multiple_deps)
    T(test_missing_name)
    T(test_missing_version)
    T(test_missing_dep_version)
    T(test_quotes_rejected)
    T(test_array_spaces_rejected)
    T(test_array_trailing_text_rejected)
    T(test_empty_array)
    T(test_whitespace_trimming)
    T(test_multiline_array)
    T(test_full_manifest)

    if (error) fprintf(stderr, "manifest tests: %d FAILED\n", error);
    return error;
}
