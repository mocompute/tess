#module main
#import <Str.tl>

// Module-level constant: weak unsigned literal 0u with Byte annotation
// should narrow to Byte (CUnsignedChar), not default to UInt (CUnsignedLongLong).
_NUL: Byte := 0u

main() {
    s := Str.from_cstr(c"abc")
    b := unwrap(Str.byte_at(s, 0u), _NUL)
    if b == 97u { 0 } else { 1 }
}
