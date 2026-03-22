#module main

// Verify that imports inside false #ifdef blocks are skipped during
// import resolution. c"nonexistent.tl" does not exist; this test would
// fail at compile time if the import were not properly skipped.

#ifdef NONEXISTENT
#import "nonexistent.tl"
#endif

#define EXISTS
#ifndef EXISTS
#import "also_nonexistent.tl"
#endif

main() {
    0
}
