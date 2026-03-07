#module main

#define FOO

main() {

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

    error
}
