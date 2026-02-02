#module main
#import <Str.tl>

// Expected runtime failure: Str.free on a string literal triggers a fatal in DEBUG mode.
// In release mode (no DEBUG), return 1 so the test still reports as a failure.

main() {
    s := "hello"
#ifdef DEBUG
    Str.free(s.&)
#endif
    1
}
