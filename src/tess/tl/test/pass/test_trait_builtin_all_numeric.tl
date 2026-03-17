// Test that all numeric builtin types satisfy a user-defined trait via family dispatch.
// Int covers all C-signed integers, UInt covers all C-unsigned, Float covers all floats.
// Fixed-width and standalone types that can't widen to the canonical type are tested
// separately in known_failures.

#import <String.tl>

#module Describe
Describe[T] : {
    describe(x: T) -> CInt
}

// Signed integers return 1, unsigned return 2, floats return 3, Bool returns 4.
#module Int
describe(n) -> CInt { 1 }

#module UInt
describe(n) -> CInt { 2 }

#module Float
describe(f) -> CInt { 3 }

#module Bool
describe(b) -> CInt { 4 }

#module main

show[T: Describe](x: T) -> CInt { x.describe() }

main() {
    error := 0

    // === C-named signed integers (subchain C_SIGNED, all widen to Int) ===

    si1: CSignedChar := 1
    error += if show(si1) == 1 { 0 } else { 1 }

    si2: CShort := 2
    error += if show(si2) == 1 { 0 } else { 1 }

    si3: CInt := 3
    error += if show(si3) == 1 { 0 } else { 1 }

    si4: CLong := 4
    error += if show(si4) == 1 { 0 } else { 1 }

    // CLongLong = Int (canonical)
    si5: Int := 5
    error += if show(si5) == 1 { 0 } else { 1 }

    // === C-named unsigned integers (subchain C_UNSIGNED, all widen to UInt) ===

    ui1: CUnsignedChar := 1
    error += if show(ui1) == 2 { 0 } else { 1 }

    ui2: CUnsignedShort := 2
    error += if show(ui2) == 2 { 0 } else { 1 }

    ui3: CUnsignedInt := 3
    error += if show(ui3) == 2 { 0 } else { 1 }

    ui4: CUnsignedLong := 4
    error += if show(ui4) == 2 { 0 } else { 1 }

    // CUnsignedLongLong = UInt (canonical)
    ui5: UInt := 5
    error += if show(ui5) == 2 { 0 } else { 1 }

    // === Float types (subchain FLOAT, all widen to Float) ===

    f1: CFloat := 1.0
    error += if show(f1) == 3 { 0 } else { 1 }

    // CDouble = Float (canonical)
    f2: Float := 2.0
    error += if show(f2) == 3 { 0 } else { 1 }

    f3: CLongDouble := 3.0
    error += if show(f3) == 3 { 0 } else { 1 }

    // === Bool ===

    error += if show(true) == 4 { 0 } else { 1 }
    error += if show(false) == 4 { 0 } else { 1 }

    error
}
