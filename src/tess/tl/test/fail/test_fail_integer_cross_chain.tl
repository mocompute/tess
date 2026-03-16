#module main
#import <cstdint.tl>

// Cross-chain: CInt (C-signed sub-chain 1) passed to CInt32 (fixed-signed sub-chain 3) should fail.

take_int32(x: CInt32) -> CInt32 { x }

main() {
    a: CInt := 42
    take_int32(a)
    0
}
