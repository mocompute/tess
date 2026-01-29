#include "alloc.h"
#include "format.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

// Helper: run tl_format on `input` and compare against `expected`.
// Returns 0 on match, 1 on mismatch (printing diff).
static int check(allocator *alloc, char const *label, char const *input, char const *expected) {
    u32 len    = (u32)strlen(input);
    str result = tl_format(alloc, input, len, "test");
    char const *got = str_cstr(&result);
    if (strcmp(got, expected) != 0) {
        fprintf(stderr, "  FAIL [%s] (got len=%zu, expected len=%zu)\n", label, strlen(got), strlen(expected));
        fprintf(stderr, "  --- expected ---\n%s", expected);
        fprintf(stderr, "  --- got ---\n%s", got);
        str_deinit(alloc, &result);
        return 1;
    }
    str_deinit(alloc, &result);
    return 0;
}

// ---------------------------------------------------------------------------
// 1. Basic indentation
// ---------------------------------------------------------------------------
static int test_indent_basic(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "single line depth 0",
        "x := 1",
        "x := 1\n");

    error += check(alloc, "block body indented",
        "foo() {\n"
        "x := 1\n"
        "}",
        "foo() {\n"
        "    x := 1\n"
        "}\n");

    error += check(alloc, "nested blocks",
        "a() {\n"
        "b() {\n"
        "c := 1\n"
        "}\n"
        "}",
        "a() {\n"
        "    b() {\n"
        "        c := 1\n"
        "    }\n"
        "}\n");

    error += check(alloc, "} dedents",
        "f() {\n"
        "x\n"
        "}\n"
        "y",
        "f() {\n"
        "    x\n"
        "}\n"
        "\n"
        "y\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 2. Operator normalization
// ---------------------------------------------------------------------------
static int test_normalize_ops(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "binary + spaced",
        "a+b",
        "a + b\n");

    error += check(alloc, "unary minus preserved",
        "(-x)",
        "(-x)\n");

    error += check(alloc, "member -> no spaces",
        "a->b",
        "a->b\n");

    error += check(alloc, "dot no spaces",
        "a.b",
        "a.b\n");

    error += check(alloc, "comma space after",
        "f(a,b,c)",
        "f(a, b, c)\n");

    error += check(alloc, "colon space after",
        "x:int",
        "x: int\n");

    error += check(alloc, "colon no space before",
        "Shape : { x : Int }",
        "Shape: { x: Int }\n");

    error += check(alloc, "space before open brace",
        "main(){",
        "main() {\n");

    error += check(alloc, "space before open brace general",
        "f(x){0}",
        "f(x) {0}\n");

    error += check(alloc, ":= spaced",
        "x:=1",
        "x := 1\n");

    error += check(alloc, "== spaced",
        "a==b",
        "a == b\n");

    error += check(alloc, "!= spaced",
        "a!=b",
        "a != b\n");

    error += check(alloc, "<= spaced",
        "a<=b",
        "a <= b\n");

    error += check(alloc, ">= spaced",
        "a>=b",
        "a >= b\n");

    error += check(alloc, "&& spaced",
        "a&&b",
        "a && b\n");

    error += check(alloc, "|| spaced",
        "a||b",
        "a || b\n");

    error += check(alloc, "scientific notation preserved",
        "x := 1e-2",
        "x := 1e-2\n");

    error += check(alloc, "string not modified",
        "s := \"a+b\"",
        "s := \"a+b\"\n");

    error += check(alloc, "char not modified",
        "c := 'x'",
        "c := 'x'\n");

    error += check(alloc, "comment not modified",
        "x := 1 // a+b",
        "x := 1                                  // a+b\n");

    error += check(alloc, "arity syntax preserved",
        "foo/2",
        "foo/2\n");

    error += check(alloc, ".* not spaced as binary",
        "a.*",
        "a.*\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 3. Blank lines
// ---------------------------------------------------------------------------
static int test_blank_lines(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "max 2 consecutive blanks",
        "a\n\n\n\n\nb",
        "a\n\n\n"
        "b\n");

    error += check(alloc, "blank between multi-line constructs",
        "f() {\n"
        "x\n"
        "}\n"
        "g() {\n"
        "y\n"
        "}",
        "f() {\n"
        "    x\n"
        "}\n"
        "\n"
        "g() {\n"
        "    y\n"
        "}\n");

    error += check(alloc, "single-line toplevel no forced blank",
        "a := 1\n"
        "b := 2",
        "a := 1\n"
        "b := 2\n");

    error += check(alloc, "no blank line between comment and multi-line construct",
        "// The Array struct.\n"
        "Array(T): {\n"
        "    v: Ptr(T),\n"
        "}",
        "// The Array struct.\n"
        "Array(T): {\n"
        "    v: Ptr(T),\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 4. Directives
// ---------------------------------------------------------------------------
static int test_directives(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "#module at indent 0",
        "  #module foo",
        "#module foo\n");

    error += check(alloc, "#import at indent 0",
        "  #import bar",
        "#import bar\n");

    error += check(alloc, "#ifc/#endc verbatim",
        "#ifc\n"
        "  int x = 1;\n"
        "#endc",
        "#ifc\n"
        "  int x = 1;\n"
        "#endc\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 5. Continuation: paren alignment
// ---------------------------------------------------------------------------
static int test_continuation_paren(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "multi-line call aligned after (",
        "f() {\n"
        "foo(a,\n"
        "b)\n"
        "}",
        "f() {\n"
        "    foo(a,\n"
        "        b)\n"
        "}\n");

    error += check(alloc, "single-line call unchanged",
        "f() {\n"
        "foo(a, b)\n"
        "}",
        "f() {\n"
        "    foo(a, b)\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 6. Continuation: binary op at end of line
// ---------------------------------------------------------------------------
static int test_continuation_binop(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "if continuation +3",
        "f() {\n"
        "if cond &&\n"
        "cond2 {\n"
        "x\n"
        "}\n"
        "}",
        "f() {\n"
        "    if cond &&\n"
        "       cond2 {\n"
        "        x\n"
        "    }\n"
        "}\n");

    error += check(alloc, "while continuation +6",
        "f() {\n"
        "while cond ||\n"
        "cond2 {\n"
        "x\n"
        "}\n"
        "}",
        "f() {\n"
        "    while cond ||\n"
        "          cond2 {\n"
        "        x\n"
        "    }\n"
        "}\n");

    error += check(alloc, ".* does not trigger continuation",
        "f() {\n"
        "a.*\n"
        "b\n"
        "}",
        "f() {\n"
        "    a.*\n"
        "    b\n"
        "}\n");

    error += check(alloc, ".& does not trigger continuation",
        "f() {\n"
        "a.&\n"
        "b\n"
        "}",
        "f() {\n"
        "    a.&\n"
        "    b\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 7. Continuation: next line starts with binary op
// ---------------------------------------------------------------------------
static int test_continuation_next_line_binop(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "next line starts with &&",
        "f() {\n"
        "if cond\n"
        "&& cond2 {\n"
        "x\n"
        "}\n"
        "}",
        "f() {\n"
        "    if cond\n"
        "       && cond2 {\n"
        "        x\n"
        "    }\n"
        "}\n");

    error += check(alloc, "next line starts with +",
        "f() {\n"
        "x\n"
        "+ y\n"
        "}",
        "f() {\n"
        "    x\n"
        "        + y\n"
        "}\n");

    // * is ambiguous (could be dereference), so NOT continuation
    error += check(alloc, "next line starts with * not continuation",
        "f() {\n"
        "x\n"
        "*y\n"
        "}",
        "f() {\n"
        "    x\n"
        "    *y\n"
        "}\n");

    error += check(alloc, "comment lines not indented as continuation",
        "// line 1\n"
        "//\n"
        "// line 3\n"
        "x := 1",
        "// line 1\n"
        "//\n"
        "// line 3\n"
        "x := 1\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 8. Pipe alignment
// ---------------------------------------------------------------------------
static int test_pipe_alignment(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "tagged union pipes aligned",
        "Name : | A\n"
        "| B\n"
        "| C",
        "Name: | A\n"
        "      | B\n"
        "      | C\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 9. Align colon-value
// ---------------------------------------------------------------------------
static int test_align_colon_value(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "colon value aligned",
        "Point : {\n"
        "x: int\n"
        "yy: int\n"
        "}",
        "Point: {\n"
        "    x:  int\n"
        "    yy: int\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 10. Align :=
// ---------------------------------------------------------------------------
static int test_align_coloneq(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, ":= aligned in sub-runs",
        "f() {\n"
        "x := 1\n"
        "yy := 2\n"
        "}",
        "f() {\n"
        "    x  := 1\n"
        "    yy := 2\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 11. Align =
// ---------------------------------------------------------------------------
static int test_align_eq(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "= aligned excluding != ==",
        "f() {\n"
        "x = 1\n"
        "yy = 2\n"
        "}",
        "f() {\n"
        "    x  = 1\n"
        "    yy = 2\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 12. Align ->
// ---------------------------------------------------------------------------
static int test_align_arrow(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "return arrows aligned",
        "f(x: int) -> int\n"
        "g(x: int, y: int) -> int",
        "f(x: int)         -> int\n"
        "g(x: int, y: int) -> int\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 13. Align paren/brace outside function bodies
// ---------------------------------------------------------------------------
static int test_align_paren_brace(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "( aligned outside function",
        "f(x: int) -> int\n"
        "g(x: int, y: int) -> int",
        "f(x: int)         -> int\n"
        "g(x: int, y: int) -> int\n");

    // Inside function body (non-struct opener), paren alignment is skipped
    error += check(alloc, "( not aligned inside function",
        "f(x: int) -> int {\n"
        "foo(1)\n"
        "barbaz(2)\n"
        "}",
        "f(x: int) -> int {\n"
        "    foo(1)\n"
        "    barbaz(2)\n"
        "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 14. Align same-line comments
// ---------------------------------------------------------------------------
static int test_align_comments(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "same-line // aligned",
        "f() {\n"
        "x := 1 // first\n"
        "yy := 2 // second\n"
        "}",
        "f() {\n"
        "    x  := 1                             // first\n"
        "    yy := 2                             // second\n"
        "}\n");

    error += check(alloc, "comment-only line unaffected",
        "// hello\n"
        "x := 1",
        "// hello\n"
        "x := 1\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 15. Idempotency
// ---------------------------------------------------------------------------
static int test_idempotency(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    char const *inputs[] = {
        "f() {\n    x := 1\n    y := 2\n}\n",
        "Name: | A\n      | B\n      | C\n",
        "#module foo\n#import bar\n\nf(x: int) -> int\n",
        "f() {\n    if cond &&\n       cond2 {\n        x\n    }\n}\n",
        "Point: {\n    x:  int\n    yy: int\n}\n",
    };
    int n = (int)(sizeof(inputs) / sizeof(inputs[0]));

    for (int i = 0; i < n; i++) {
        char label[64];
        snprintf(label, sizeof(label), "idempotent #%d", i);
        u32 len    = (u32)strlen(inputs[i]);
        str first  = tl_format(alloc, inputs[i], len, "test");
        char const *fbuf = str_cstr(&first);
        u32 flen   = (u32)str_len(first);
        str second = tl_format(alloc, fbuf, flen, "test");
        char const *sbuf = str_cstr(&second);
        if (strcmp(fbuf, sbuf) != 0) {
            fprintf(stderr, "  FAIL [%s]\n", label);
            fprintf(stderr, "  --- first ---\n%s", fbuf);
            fprintf(stderr, "  --- second ---\n%s", sbuf);
            error++;
        }
        str_deinit(alloc, &second);
        str_deinit(alloc, &first);
    }

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 16. Edge cases
// ---------------------------------------------------------------------------
static int test_edge_cases(void) {
    int error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "empty input",
        "",
        "\n");

    error += check(alloc, "whitespace only",
        "   \n  \n   ",
        "\n");

    error += check(alloc, "line with only }",
        "f() {\n"
        "x\n"
        "}",
        "f() {\n"
        "    x\n"
        "}\n");

    error += check(alloc, "trailing newline normalization",
        "x := 1\n\n\n",
        "x := 1\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int error      = 0;
    int this_error = 0;

    T(test_indent_basic);
    T(test_normalize_ops);
    T(test_blank_lines);
    T(test_directives);
    T(test_continuation_paren);
    T(test_continuation_binop);
    T(test_continuation_next_line_binop);
    T(test_pipe_alignment);
    T(test_align_colon_value);
    T(test_align_coloneq);
    T(test_align_eq);
    T(test_align_arrow);
    T(test_align_paren_brace);
    T(test_align_comments);
    T(test_idempotency);
    T(test_edge_cases);

    return error;
}
