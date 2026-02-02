#module main

#define OUTER

main() {

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
