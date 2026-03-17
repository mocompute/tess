#include "alloc.h"
#include "format.h"
#include "str.h"

#include <stdio.h>
#include <string.h>

// Helper: run tl_format on `input` and compare against `expected`.
// Returns 0 on match, 1 on mismatch (printing diff).
static int check(allocator *alloc, char const *label, char const *input, char const *expected) {
    u32         len    = (u32)strlen(input);
    str         result = tl_format(alloc, input, len, "test");
    char const *got    = str_cstr(&result);
    if (strcmp(got, expected) != 0) {
        fprintf(stderr, "  FAIL [%s] (got len=%zu, expected len=%zu)\n", label, strlen(got),
                strlen(expected));
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
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "single line depth 0", "x := 1", "x := 1\n");

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

    error +=
      check(alloc, "}} dedents two levels", "a() {\nb() {\nx\n}}", "a() {\n    b() {\n        x\n}}\n");

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
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "binary + spaced", "a+b", "a + b\n");

    error += check(alloc, "unary minus preserved", "(-x)", "(-x)\n");

    error += check(alloc, "member -> no spaces", "a->b", "a->b\n");

    error += check(alloc, "dot no spaces", "a.b", "a.b\n");

    error += check(alloc, "comma space after", "f(a,b,c)", "f(a, b, c)\n");

    error += check(alloc, "colon space after", "x:int", "x: int\n");

    error += check(alloc, "colon no space before", "Shape : { x : Int }", "Shape: { x: Int }\n");

    error += check(alloc, "space before open brace", "main(){", "main() {\n");

    error += check(alloc, "space before open brace general", "f(x){0}", "f(x) {0}\n");

    error += check(alloc, ":= spaced", "x:=1", "x := 1\n");

    error += check(alloc, "== spaced", "a==b", "a == b\n");

    error += check(alloc, "!= spaced", "a!=b", "a != b\n");

    error += check(alloc, "<= spaced", "a<=b", "a <= b\n");

    error += check(alloc, ">= spaced", "a>=b", "a >= b\n");

    error += check(alloc, "&& spaced", "a&&b", "a && b\n");

    error += check(alloc, "|| spaced", "a||b", "a || b\n");

    error += check(alloc, "scientific notation preserved", "x := 1e-2", "x := 1e-2\n");

    error += check(alloc, "string not modified", "s := \"a+b\"", "s := \"a+b\"\n");

    error += check(alloc, "char not modified", "c := 'x'", "c := 'x'\n");

    error += check(alloc, "comment not modified", "x := 1 // a+b",
                   "x := 1                                  // a+b\n");

    error += check(alloc, "arity syntax preserved", "foo/2", "foo/2\n");

    error += check(alloc, ".* not spaced as binary", "a.*", "a.*\n");

    error += check(alloc, "type args attached to name", "foo [T](x: T)", "foo[T](x: T)\n");

    error += check(alloc, "type args no space before (", "foo[T] (x: T)", "foo[T](x: T)\n");

    error += check(alloc, "type args already attached", "foo[T](x: T)", "foo[T](x: T)\n");

    error += check(alloc, "nested type args", "foo [Array[Int]](x: Int)", "foo[Array[Int]](x: Int)\n");

    error += check(alloc, "dot-bracket indexing", "arr.[0]", "arr.[0]\n");

    error += check(alloc, "arity after type args", "foo[Int]/1", "foo[Int]/1\n");

    error += check(alloc, "arity after nested type args", "foo[Array[Int]]/2", "foo[Array[Int]]/2\n");

    error += check(alloc, "<<= compound assign", "s <<= four", "s <<= four\n");

    error += check(alloc, ">>= compound assign", "s >>= four", "s >>= four\n");

    error += check(alloc, "<<= no spaces", "s<<=four", "s <<= four\n");

    error += check(alloc, ">>= no spaces", "s>>=four", "s >>= four\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 3. Blank lines
// ---------------------------------------------------------------------------
static int test_blank_lines(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "max 2 consecutive blanks", "a\n\n\n\n\nb",
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
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "#module at indent 0", "  #module foo", "#module foo\n");

    error += check(alloc, "#import at indent 0", "  #import bar", "#import bar\n");

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
    int        error = 0;
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
    int        error = 0;
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
    int        error = 0;
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

    error += check(alloc, "negative literal does not trigger continuation",
                   "f() {\n"
                   "while i < n {\n"
                   "i = i + 1\n"
                   "}\n"
                   "-1\n"
                   "}",
                   "f() {\n"
                   "    while i < n {\n"
                   "        i = i + 1\n"
                   "    }\n"
                   "    -1\n"
                   "}\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 8. Pipe alignment
// ---------------------------------------------------------------------------
static int test_pipe_alignment(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "tagged union pipes aligned",
                   "Name : | A\n"
                   "| B\n"
                   "| C",
                   "Name: | A\n"
                   "      | B\n"
                   "      | C\n");

    error += check(alloc, "indented comment between pipe variants", "Name: | A\n      // comment\n| B",
                   "Name: | A\n"
                   "      // comment\n"
                   "      | B\n");

    error += check(alloc, "left-margin comment in pipe group stays at margin", "Name: | A\n// comment\n| B",
                   "Name: | A\n"
                   "// comment\n"
                   "      | B\n");

    error += check(alloc, "C union pipes aligned",
                   "CUnion { | A\n"
                   "| B\n"
                   "| C",
                   "CUnion { | A\n"
                   "         | B\n"
                   "         | C\n");

    error += check(alloc, "blank line resets pipe alignment",
                   "Option(T): | Some { v: T }\n           | None\n\n// Result type\n\nResult(T, E): | "
                   "Ok(T)\n              | Err(E)\n",
                   "Option(T): | Some { v: T }\n"
                   "           | None\n"
                   "\n"
                   "// Result type\n"
                   "\n"
                   "Result(T, E): | Ok(T)\n"
                   "              | Err(E)\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 9. Align colon-value
// ---------------------------------------------------------------------------
static int test_align_colon_value(void) {
    int        error = 0;
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
    int        error = 0;
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
    int        error = 0;
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
    int        error = 0;
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
    int        error = 0;
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
    int        error = 0;
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
// 15. Align with type parameter constraints (bracket depth)
// ---------------------------------------------------------------------------
static int test_align_bracket_constraints(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    // Constraint colons inside [K: HashEq, V] must NOT be picked up
    // by colon-value alignment. The ( and -> should align cleanly.
    error += check(alloc, "synopsis with constraints aligns ( and ->",
                   "set[K: HashEq, V](self: Ptr[T], key: K, value: V) -> Void\n"
                   "get[K: HashEq, V](self: Ptr[T], key: K) -> Ptr[V]\n"
                   "get_copy[K: HashEq, V](self: Ptr[T], key: K) -> Option[V]\n"
                   "contains[K: HashEq, V](self: Ptr[T], key: K) -> Bool\n"
                   "remove[K: HashEq, V](self: Ptr[T], key: K) -> Bool",
                   "set[K: HashEq, V]     (self: Ptr[T], key: K, value: V) -> Void\n"
                   "get[K: HashEq, V]     (self: Ptr[T], key: K)           -> Ptr[V]\n"
                   "get_copy[K: HashEq, V](self: Ptr[T], key: K)           -> Option[V]\n"
                   "contains[K: HashEq, V](self: Ptr[T], key: K)           -> Bool\n"
                   "remove[K: HashEq, V]  (self: Ptr[T], key: K)           -> Bool\n");

    // Simpler case: no constraints, colon-value should still work outside brackets
    error += check(alloc, "no constraints still aligns ( and ->",
                   "f[T](x: T) -> T\n"
                   "gg[T](x: T, y: T) -> T",
                   "f[T] (x: T)       -> T\n"
                   "gg[T](x: T, y: T) -> T\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 16. Idempotency
// ---------------------------------------------------------------------------
static int test_idempotency(void) {
    int         error    = 0;
    allocator  *alloc    = arena_create(default_allocator(), 4096);

    char const *inputs[] = {
      "f() {\n    x := 1\n    y := 2\n}\n",
      "Name: | A\n      | B\n      | C\n",
      "#module foo\n#import bar\n\nf(x: int) -> int\n",
      "f() {\n    if cond &&\n       cond2 {\n        x\n    }\n}\n",
      "Point: {\n    x:  int\n    yy: int\n}\n",
      "a() {\n    b() {\n        x\n}}\n",
      "Name: | A\n      // comment\n      | B\n",
      "set[K: HashEq, V]     (self: Ptr[T], key: K, value: V) -> Void\n"
      "get[K: HashEq, V]     (self: Ptr[T], key: K)           -> Ptr[V]\n"
      "get_copy[K: HashEq, V](self: Ptr[T], key: K)           -> Option[V]\n"
      "contains[K: HashEq, V](self: Ptr[T], key: K)           -> Bool\n"
      "remove[K: HashEq, V]  (self: Ptr[T], key: K)           -> Bool\n",
    };
    int n = (int)(sizeof(inputs) / sizeof(inputs[0]));

    for (int i = 0; i < n; i++) {
        char label[64];
        snprintf(label, sizeof(label), "idempotent #%d", i);
        u32         len    = (u32)strlen(inputs[i]);
        str         first  = tl_format(alloc, inputs[i], len, "test");
        char const *fbuf   = str_cstr(&first);
        u32         flen   = (u32)str_len(first);
        str         second = tl_format(alloc, fbuf, flen, "test");
        char const *sbuf   = str_cstr(&second);
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
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    error += check(alloc, "empty input", "", "\n");

    error += check(alloc, "whitespace only", "   \n  \n   ", "\n");

    error += check(alloc, "line with only }",
                   "f() {\n"
                   "x\n"
                   "}",
                   "f() {\n"
                   "    x\n"
                   "}\n");

    error += check(alloc, "trailing newline normalization", "x := 1\n\n\n", "x := 1\n");

    arena_destroy(&alloc);
    return error;
}

// ---------------------------------------------------------------------------
// 16. Attribute bracket [[ ]] syntax
// ---------------------------------------------------------------------------
static int test_attribute_brackets(void) {
    int        error = 0;
    allocator *alloc = arena_create(default_allocator(), 4096);

    // Space preserved between := and [[ and between ]] and (
    error += check(alloc, "alloc closure basic", "f := [[alloc, capture(n)]](x) { x + n }",
                   "f := [[alloc, capture(n)]] (x) { x + n }\n");

    // Already formatted — idempotent
    error += check(alloc, "alloc closure already formatted", "f := [[alloc, capture(n)]] (x) { x + n }",
                   "f := [[alloc, capture(n)]] (x) { x + n }\n");

    // Regular type args still attach to identifier (no space)
    error += check(alloc, "type args still attach", "foo [T](x) { x }", "foo[T](x) { x }\n");

    // Attribute with block body
    error += check(alloc, "alloc closure with block", "f:=[[alloc,capture(a,b)]](x){\nx+a+b\n}",
                   "f := [[alloc, capture(a, b)]] (x) {\n"
                   "    x + a + b\n"
                   "}\n");

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
    T(test_align_bracket_constraints);
    T(test_idempotency);
    T(test_edge_cases);
    T(test_attribute_brackets);

    return error;
}
