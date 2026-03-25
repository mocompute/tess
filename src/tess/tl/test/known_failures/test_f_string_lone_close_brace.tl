// Lone } in f-string literal should produce a clear error,
// but currently corrupts the parser state (reports no_main_function).
#module main
#import <Print.tl>

main() {
    s := f"hello } world"
    0
}
