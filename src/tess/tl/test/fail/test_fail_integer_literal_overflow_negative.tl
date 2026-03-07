#module main
#import <stdint.tl>

// Compile-time range checking: literal -129 resolving to CInt8 should be
// a compile-time error (CInt8 range is -128..127).

main() {
    a: CInt8 := -129
    0
}
