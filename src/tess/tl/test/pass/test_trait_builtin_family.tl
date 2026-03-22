// Test that narrow integer types share their family's trait implementations.
// CShort has module c"Int", so Int__describe__1 is found directly via UFCS.

#import <String.tl>

#module Describe

Describe[T]: {
    describe(x: T) -> String
}

#module Int
describe(n) -> String { String.from_int(n) }

#module main

show[T: Describe](x: T) -> String { x.describe() }

main() {
    error := 0

    // CShort has module c"Int" — finds Int__describe__1 directly
    n: CShort := 42
    s         := show(n)
    error += if String.eq(s, String.from_cstr(c"42")) { 0 } else { 1 }

    // CInt also has module c"Int"
    m: CInt := 99
    s2      := show(m)
    error += if String.eq(s2, String.from_cstr(c"99")) { 0 } else { 1 }

    error
}
