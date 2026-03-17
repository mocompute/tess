// Test bitwise operator overloading on user-defined types.

#module Bits

Mask: { val: Int }

bit_and(a: Mask, b: Mask) -> Mask { Mask(val = a.val & b.val)   }
bit_or (a: Mask, b: Mask) -> Mask { Mask(val = a.val | b.val)   }
bit_xor(a: Mask, b: Mask) -> Mask { Mask(val = a.val ^ b.val)   }
shl    (a: Mask, b: Mask) -> Mask { Mask(val = a.val << b.val)  }
shr    (a: Mask, b: Mask) -> Mask { Mask(val = a.val >> b.val)  }
bit_not(v: Mask)          -> Mask          { Mask(val = ~v.val) }
eq     (a: Mask, b: Mask) -> Bool { a.val == b.val              }

#module main

main() {
    error := 0

    a := Bits.Mask(val = 0xFF)
    b := Bits.Mask(val = 0x0F)

    // & dispatches to Bits.bit_and
    c := a & b
    error += if c.val == 0x0F {0} else {1}

    // | dispatches to Bits.bit_or
    d := a | b
    error += if d.val == 0xFF {0} else {1}

    // ^ dispatches to Bits.bit_xor
    e := a ^ b
    error += if e.val == 0xF0 {0} else {1}

    // << dispatches to Bits.shl
    one  := Bits.Mask(val = 1)
    four := Bits.Mask(val = 4)
    f    := one << four
    error += if f.val == 16 {0} else {1}

    // >> dispatches to Bits.shr
    sixteen := Bits.Mask(val = 16)
    g       := sixteen >> four
    error += if g.val == 1 {0} else {1}

    // ~ dispatches to Bits.bit_not
    zero := Bits.Mask(val = 0)
    h    := ~zero
    error += if h.val == ~0 {0} else {1}

    // Compound assignment &=
    x := Bits.Mask(val = 0xFF)
    x &= b
    error += if x.val == 0x0F {0} else {1}

    // Compound assignment |=
    y := Bits.Mask(val = 0xF0)
    y |= b
    error += if y.val == 0xFF {0} else {1}

    // Compound assignment ^=
    z := Bits.Mask(val = 0xFF)
    z ^= b
    error += if z.val == 0xF0 {0} else {1}

    // Compound assignment <<=
    s := Bits.Mask(val = 1)
    s <<= four
    error += if s.val == 16 {0} else {1}

    // Compound assignment >>=
    t := Bits.Mask(val = 16)
    t >>= four
    error += if t.val == 1 {0} else {1}

    error
}
