// An invalid format specifier should produce a compile error.
#module main
#import <Print.tl>

main() {
    x := 42
    s := f"{x:???}"
    0
}
