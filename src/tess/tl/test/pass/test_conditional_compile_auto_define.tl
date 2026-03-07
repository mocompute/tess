#module main

// DEBUG or NDEBUG is always auto-defined by the compiler.
// Verify that at least one is present.

main() {
    have_debug := 0

#ifdef DEBUG
    have_debug = 1
#endif

#ifdef NDEBUG
    have_debug = 1
#endif

    if have_debug == 1 {0} else {1}
}
