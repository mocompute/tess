#module main
#import <String.tl>
#import <Hash.tl>

main() {
    error := 0

    // s"..." uses string_literals allocator; from_cstr uses default allocator.
    // Equality, hashing, concatenation, and length must work across allocator boundaries.

    lit := s"hello"
    dyn := String.from_cstr("hello")

    // Same content must be equal regardless of allocator
    error += if lit.eq(dyn) { 0 } else { 1 }
    error += if dyn.eq(lit) { 0 } else { 1 }

    // Lengths must match
    error += if lit.len() == dyn.len() { 0 } else { 1 }
    error += if lit.len() == 5zu { 0 } else { 1 }

    // Hashes must match (same content -> same hash)
    error += if lit.hash() == dyn.hash() { 0 } else { 1 }

    // Concatenation across allocator boundaries
    cat1 := String.cat(lit, dyn)
    cat2 := String.cat(dyn, lit)
    error += if cat1.eq(s"hellohello") { 0 } else { 1 }
    error += if cat2.eq(s"hellohello") { 0 } else { 1 }
    error += if cat1.eq(cat2) { 0 } else { 1 }

    // cat_3 mixing literal and dynamic
    cat3 := String.cat_3(lit, s" ", dyn)
    error += if cat3.eq(s"hello hello") { 0 } else { 1 }

    // Empty literal vs empty dynamic
    empty_lit := s""
    empty_dyn := String.from_cstr("")
    error += if empty_lit.eq(empty_dyn) { 0 } else { 1 }
    error += if empty_lit.len() == 0zu { 0 } else { 1 }
    error += if empty_lit.hash() == empty_dyn.hash() { 0 } else { 1 }

    // Long string (exceeds SSO threshold of 14 bytes) — forces heap allocation
    long_lit := s"this is a longer string that exceeds sso"
    long_dyn := String.from_cstr("this is a longer string that exceeds sso")
    error += if long_lit.eq(long_dyn) { 0 } else { 1 }
    error += if long_lit.hash() == long_dyn.hash() { 0 } else { 1 }
    error += if long_lit.len() == long_dyn.len() { 0 } else { 1 }

    error
}
