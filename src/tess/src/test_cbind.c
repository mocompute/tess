#include "alloc.h"
#include "cbind.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

// Helper: feed preprocessed text through cbind parser and compare output.
// target_file controls which declarations are included (via line marker filtering).
// Returns 0 on match, 1 on mismatch.
static int check(allocator *alloc, char const *label, char const *target_file,
                 char const *module_name, char const *input, char const *expected) {
    u32         len    = (u32)strlen(input);
    str         result = tl_cbind_from_preprocessed(alloc, input, len, target_file, module_name);
    char const *got    = str_cstr(&result);
    if (strcmp(got, expected) != 0) {
        fprintf(stderr, "  FAIL [%s]\n", label);
        fprintf(stderr, "  --- expected (len=%zu) ---\n%s", strlen(expected), expected);
        fprintf(stderr, "  --- got (len=%zu) ---\n%s", strlen(got), got);
        str_deinit(alloc, &result);
        return 1;
    }
    str_deinit(alloc, &result);
    return 0;
}

// ---------------------------------------------------------------------------
// 1. Simple function
// ---------------------------------------------------------------------------
static int test_simple_function(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "simple function", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int foo(int x);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_foo(x: CInt) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 2. Void function
// ---------------------------------------------------------------------------
static int test_void_function(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "void function", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "void bar(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_bar() -> Void\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 3. Const char pointer
// ---------------------------------------------------------------------------
static int test_const_char_ptr(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "const char *", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int puts(const char *s);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_puts(s: Ptr[Const[CChar]]) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 4. void* return
// ---------------------------------------------------------------------------
static int test_void_star_return(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "void* return", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "void *malloc(unsigned long size);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_malloc(size: CUnsignedLong) -> Ptr[any]\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 5. Variadic function
// ---------------------------------------------------------------------------
static int test_variadic(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "variadic", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int printf(const char *fmt, ...);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_printf(fmt: Ptr[Const[CChar]], args: ...) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 6. Struct with fields
// ---------------------------------------------------------------------------
static int test_struct(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "struct", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "struct point { int x; int y; };\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct_point: { x: CInt, y: CInt }\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 7. Enum values
// ---------------------------------------------------------------------------
static int test_enum(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "enum", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "enum color { RED = 0, GREEN = 1, BLUE = 2 };\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_RED: CInt\n"
                   "c_GREEN: CInt\n"
                   "c_BLUE: CInt\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 8. Forward-declared struct (opaque)
// ---------------------------------------------------------------------------
static int test_forward_struct(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "forward struct", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "struct opaque;\n"
                   "struct opaque *get_opaque(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct_opaque: { _opaque: Byte }\n"
                   "\n"
                   "c_get_opaque() -> Ptr[c_struct_opaque]\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 9. typedef struct foo foo (alias)
// ---------------------------------------------------------------------------
static int test_typedef_struct_alias(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "typedef struct alias", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "struct point { int x; int y; };\n"
                   "typedef struct point point_t;\n"
                   "point_t make_point(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct_point: { x: CInt, y: CInt }\n"
                   "\n"
                   "c_make_point() -> c_struct_point\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 10. typedef struct { ... } foo (anonymous struct)
// ---------------------------------------------------------------------------
static int test_typedef_anon_struct(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "typedef anon struct", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "typedef struct { int x; int y; } vec2;\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_vec2: { x: CInt, y: CInt }\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 11. typedef struct tag { ... } name (different names)
// ---------------------------------------------------------------------------
static int test_typedef_struct_different_names(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "typedef struct different names", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "typedef struct point_s { int x; int y; } point_t;\n"
                   "point_t make(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct_point_s: { x: CInt, y: CInt }\n"
                   "\n"
                   "c_make() -> c_struct_point_s\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 12. typedef int myint (simple type alias)
// ---------------------------------------------------------------------------
static int test_typedef_simple(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "typedef simple", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "typedef int myint;\n"
                   "myint add(myint a, myint b);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_add(a: CInt, b: CInt) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 13. Function pointer parameter
// ---------------------------------------------------------------------------
static int test_function_pointer_param(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "function pointer param", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "void set_callback(void (*cb)(int));\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_set_callback(cb: (arg0: CInt) -> Void) -> Void\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 14. #define integer constant
// ---------------------------------------------------------------------------
static int test_define_constant(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "define constant", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "#define MAX_SIZE 1024\n"
                   "#define VERSION 3\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_MAX_SIZE: CInt\n"
                   "c_VERSION: CInt\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 15. Transitive include filtering
// ---------------------------------------------------------------------------
static int test_filter_transitive(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "filter transitive", "test.h", "test",
                   "# 1 \"other.h\"\n"
                   "int other_func(void);\n"
                   "# 1 \"test.h\"\n"
                   "int my_func(void);\n"
                   "# 2 \"other.h\"\n"
                   "int other_func2(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_my_func() -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 16. Unnamed parameters
// ---------------------------------------------------------------------------
static int test_unnamed_params(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "unnamed params", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int add(int, int);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_add(arg0: CInt, arg1: CInt) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 17. Multiple type specifiers
// ---------------------------------------------------------------------------
static int test_multi_word_types(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "unsigned long long", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "unsigned long long big(unsigned short x);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_big(x: CUnsignedShort) -> CUnsignedLongLong\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 18. size_t and other standard types
// ---------------------------------------------------------------------------
static int test_standard_types(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "size_t", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "void *memcpy(void *dst, const void *src, size_t n);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_memcpy(dst: Ptr[any], src: Ptr[Const[any]], n: CSize) -> Ptr[any]\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 19. Realistic GCC output with system includes
// ---------------------------------------------------------------------------
static int test_gcc_with_system_includes(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 8192);

    // Simulates GCC output: system header defines size_t, then returns to target
    error += check(a, "gcc with system includes", "/tmp/test_inc.h", "test_inc",
                   "# 0 \"/tmp/test_inc.h\"\n"
                   "# 0 \"<built-in>\"\n"
                   "#define __STDC__ 1\n"
                   "# 0 \"<built-in>\"\n"
                   "# 1 \"/tmp/test_inc.h\"\n"
                   "# 1 \"/some/stddef.h\" 1 3 4\n"
                   "typedef unsigned long size_t;\n"
                   "# 2 \"/tmp/test_inc.h\" 2\n"
                   "#define MYCONST 42\n"
                   "\n"
                   "# 3 \"/tmp/test_inc.h\"\n"
                   "void *myfunc(size_t n);\n",
                   "#module test_inc\n"
                   "#include \"/tmp/test_inc.h\"\n"
                   "\n"
                   "c_MYCONST: CInt\n"
                   "\n"
                   "c_myfunc(n: CSize) -> Ptr[any]\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

#define T(name)                                                                                    \
    {                                                                                              \
        this_error = name();                                                                       \
        if (this_error) fprintf(stderr, "FAIL: " #name " (%d)\n", this_error);                     \
        error += this_error;                                                                       \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_simple_function);
    T(test_void_function);
    T(test_const_char_ptr);
    T(test_void_star_return);
    T(test_variadic);
    T(test_struct);
    T(test_enum);
    T(test_forward_struct);
    T(test_typedef_struct_alias);
    T(test_typedef_anon_struct);
    T(test_typedef_struct_different_names);
    T(test_typedef_simple);
    T(test_function_pointer_param);
    T(test_define_constant);
    T(test_filter_transitive);
    T(test_unnamed_params);
    T(test_multi_word_types);
    T(test_standard_types);
    T(test_gcc_with_system_includes);

    if (error) {
        fprintf(stderr, "\n%d test(s) failed\n", error);
    }
    return error;
}
