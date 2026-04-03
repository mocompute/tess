#module main

// Top-level defines needed by individual tests
#define FOO
#define OUTER

// test_import: Verify that imports inside false #ifdef blocks are skipped.
// These must stay at the top level since they are import directives.
#ifdef NONEXISTENT
#import "nonexistent.tl"
#endif

#define EXISTS
#ifndef EXISTS
#import "also_nonexistent.tl"
#endif

// ---------------------------------------------------------------------------
// test_define: #define, #undef, #ifdef, #ifndef, repeated #define
// ---------------------------------------------------------------------------
test_define() {

    error := 0

    // Unlike C, repeated #define is ok, since symbols are just boolean
#define FOO

#ifdef FOO
    val := 1
#endif

#ifndef FOO
    val := 2
#endif

    error += if val == 1 {0} else {1}

#undef FOO

#ifdef FOO
    val := 1
#endif

#ifndef FOO
    val := 2
#endif

    error += if val == 2 {0} else {1}

    // Restore FOO for any later use
#define FOO

    error
}

// ---------------------------------------------------------------------------
// test_cc_defines: C compiler predefined macros (__linux__, __APPLE__, _WIN32)
// ---------------------------------------------------------------------------
test_cc_defines() {
    x := 1
#ifdef __linux__
    x = 0
#endif
#ifdef __APPLE__
    x = 0
#endif
#ifdef _WIN32
    x = 0
#endif
    return x
}

// ---------------------------------------------------------------------------
// test_auto_define: DEBUG or NDEBUG is always auto-defined
// ---------------------------------------------------------------------------
test_auto_define() {
    have_debug := 0

#ifdef DEBUG
    have_debug = 1
#endif

#ifdef NDEBUG
    have_debug = 1
#endif

    if have_debug == 1 {0} else {1}
}

// ---------------------------------------------------------------------------
// test_import: conditional import skip (logic is at the top level above)
// ---------------------------------------------------------------------------
test_import() {
    0
}

// ---------------------------------------------------------------------------
// test_nested: nested #ifdef / #ifndef blocks
// ---------------------------------------------------------------------------
test_nested() {

    error := 0

    // Nested ifdef: inner block skipped, outer block active
#ifdef OUTER
#ifdef INNER
    val := 99
#endif
    val := 1
#endif

    error += if val == 1 {0} else {1}

    // Nested ifdef: outer block skipped with nested ifdef inside.
    // The inner #ifdef and its #endif must be properly tracked so
    // the outer #endif is not consumed by the inner block.
    val := 2
#ifdef UNDEFINED
    val := 90
#ifdef ALSO_UNDEFINED
    val := 91
#endif
    val := 92
#endif

    error += if val == 2 {0} else {1}

    // Nested ifndef inside ifdef
#ifdef OUTER
#ifndef UNDEFINED
    val := 3
#endif
#endif

    error += if val == 3 {0} else {1}

    // Triple nesting: outer true, middle false, inner irrelevant
    val := 4
#ifdef OUTER
#ifdef UNDEFINED
#ifdef OUTER
    val := 99
#endif
#endif
#endif

    error += if val == 4 {0} else {1}

    error
}

// ---------------------------------------------------------------------------
// main: run all tests
// ---------------------------------------------------------------------------
main() {
    test_define() + test_cc_defines() + test_auto_define() + test_import() + test_nested()
}
