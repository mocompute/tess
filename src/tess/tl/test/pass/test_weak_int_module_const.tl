#module main
#import <String.tl>

// Module-level constant: weak unsigned literal 0u with Byte annotation
// should narrow to Byte (CUnsignedChar), not default to UInt (CUnsignedLongLong).
_NUL: Byte := 0u

main() {
    s := String.from_cstr("abc")
    b := unwrap(String.byte_at(s, 0zu), _NUL)
    if b == 97u { 0 } else { 1 }
}
