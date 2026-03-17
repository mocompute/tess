#include "alloc.h"
#include "cbind.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

// Helper: feed preprocessed text through cbind parser and compare output.
// target_file controls which declarations are included (via line marker filtering).
// Returns 0 on match, 1 on mismatch.
static int check(allocator *alloc, char const *label, char const *target_file, char const *module_name,
                 char const *input, char const *expected) {
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
                   "c_make_point() -> c_point_t\n");

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
                   "c_make() -> c_point_t\n");

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
// 20. __builtin_va_list as type
// ---------------------------------------------------------------------------
static int test_builtin_va_list(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "builtin_va_list", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int vprintf(const char *fmt, __builtin_va_list ap);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_vprintf(fmt: Ptr[Const[CChar]], ap: c___builtin_va_list) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 21. Struct with mixed pointer declarators
// ---------------------------------------------------------------------------
static int test_struct_mixed_pointers(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "mixed pointer declarators", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "struct S { int *a, b, **c; };\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct_S: { a: Ptr[CInt], b: CInt, c: Ptr[Ptr[CInt]] }\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 22. Enum with negative values
// ---------------------------------------------------------------------------
static int test_enum_negative(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "enum negative values", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "enum status { OK = 0, ERR = -1, NOMEM = -2 };\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_OK: CInt\n"
                   "c_ERR: CInt\n"
                   "c_NOMEM: CInt\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 23. Struct with function pointer field
// ---------------------------------------------------------------------------
static int test_struct_fp_field(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "struct fp field", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "struct ops { void (*init)(int); int (*get)(void); };\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct_ops: { init: (arg0: CInt) -> Void, get: () -> CInt }\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 24. Static inline function body is skipped
// ---------------------------------------------------------------------------
static int test_static_inline_skipped(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    // static inline should be skipped; only the regular function should appear
    error += check(a, "static inline skipped", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "static inline int square(int x) { return x * x; }\n"
                   "int cube(int x);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_cube(x: CInt) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 25. C11 attributes don't break parsing
// ---------------------------------------------------------------------------
static int test_c11_attributes(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "c11 attributes", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "_Alignas(16) int aligned_func(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_aligned_func() -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 26. Define followed by function (tokenizer doesn't eat +)
// ---------------------------------------------------------------------------
static int test_define_then_function(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "define then function", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "#define VERSION 1\n"
                   "int init(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_VERSION: CInt\n"
                   "\n"
                   "c_init() -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 27. Embedded line markers in multi-line declarations
// ---------------------------------------------------------------------------
static int test_embedded_line_markers(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    // Simulates GCC output where __attribute__ and ; are on separate lines
    // with line markers interspersed (the malloc pattern)
    error += check(a, "embedded line markers", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "extern void *malloc (size_t __size)\n"
                   "# 5 \"test.h\" 3 4\n"
                   "                  __attribute__ ((__nothrow__)) __attribute__ ((__malloc__))\n"
                   "# 7 \"test.h\" 3 4\n"
                   ";\n"
                   "extern void free (void *__ptr)\n"
                   "# 10 \"test.h\" 3 4\n"
                   "                 __attribute__ ((__nothrow__))\n"
                   ";\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_malloc(size: CSize) -> Ptr[any]\n"
                   "c_free(ptr: Ptr[any]) -> Void\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 28. #define inside struct body (glibc fd_set pattern)
// ---------------------------------------------------------------------------
static int test_define_inside_struct(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    // A #define inside a struct body must be skipped.
    // The parser must continue parsing fields and subsequent decls.
    error += check(a, "define inside struct", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "typedef struct\n"
                   "  {\n"
                   "    int count;\n"
                   "#define MAGIC_COUNT 42\n"
                   "    long int flags;\n"
                   "  } my_data;\n"
                   "int process(my_data *d);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_my_data: { count: CInt, flags: CLong }\n"
                   "\n"
                   "c_process(d: Ptr[c_my_data]) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 29. Array field inside struct
// ---------------------------------------------------------------------------
static int test_struct_array_field(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "struct array field", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "typedef struct { int data[32]; char name[64]; } buffer;\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_buffer: { data: CArray[CInt, 32], name: CArray[CChar, 64] }\n"
                   "\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 30. Skip __ functions
// ---------------------------------------------------------------------------
static int test_skip_dunder_functions(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "skip __ functions", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int foo(int x);\n"
                   "int __bar(int y);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_foo(x: CInt) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 31. Strip __ from parameter names
// ---------------------------------------------------------------------------
static int test_strip_dunder_params(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "strip __ params", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "int foo(int __x, const char *__str);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_foo(x: CInt, str: Ptr[Const[CChar]]) -> CInt\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// Duplicate function declarations (common in glibc extern patterns)
// ---------------------------------------------------------------------------
static int test_dedup_functions(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "dedup functions", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "extern int foo(int x);\n"
                   "extern int foo(int x);\n"
                   "extern void bar(void);\n"
                   "extern void bar(void);\n"
                   "extern void bar(void);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_foo(x: CInt) -> CInt\n"
                   "c_bar() -> Void\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// 28. typedef struct _IO_FILE FILE (preserve typedef name)
// ---------------------------------------------------------------------------
static int test_typedef_preserves_name(void) {
    int        error = 0;
    allocator *a     = arena_create(default_allocator(), 4096);

    error += check(a, "typedef preserves name", "test.h", "test",
                   "# 1 \"test.h\"\n"
                   "struct _IO_FILE { int fd; };\n"
                   "typedef struct _IO_FILE FILE;\n"
                   "FILE *fopen(const char *path, const char *mode);\n",
                   "#module test\n"
                   "#include <test.h>\n"
                   "\n"
                   "c_struct__IO_FILE: { fd: CInt }\n"
                   "\n"
                   "c_fopen(path: Ptr[Const[CChar]], mode: Ptr[Const[CChar]]) -> Ptr[c_FILE]\n");

    arena_destroy(&a);
    return error;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

#define T(name)                                                                                            \
    {                                                                                                      \
        this_error = name();                                                                               \
        if (this_error) fprintf(stderr, "FAIL: " #name " (%d)\n", this_error);                             \
        error += this_error;                                                                               \
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
    T(test_builtin_va_list);
    T(test_struct_mixed_pointers);
    T(test_enum_negative);
    T(test_struct_fp_field);
    T(test_static_inline_skipped);
    T(test_c11_attributes);
    T(test_define_then_function);
    T(test_embedded_line_markers);
    T(test_define_inside_struct);
    T(test_struct_array_field);
    T(test_skip_dunder_functions);
    T(test_strip_dunder_params);
    T(test_dedup_functions);
    T(test_typedef_preserves_name);

    if (error) {
        fprintf(stderr, "\n%d test(s) failed\n", error);
    }
    return error;
}
