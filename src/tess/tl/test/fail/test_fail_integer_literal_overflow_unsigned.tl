#module main
#import <cstdint.tl>

// Compile-time range checking: unsigned literal 256u resolving to CUInt8
// should be a compile-time error (CUInt8 range is 0..255).

main() {
    a: CUInt8 := 256u
    0
}
