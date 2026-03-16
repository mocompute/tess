// Test that hash() works on user-defined types with a hash() implementation.

#module main
#import <String.tl>

compute_hash[T: Hash](x: T) -> CSize {
    hash(x)
}

main() -> CInt {
    error: CInt := 0

    // String has a hash() implementation
    s  := String.from_cstr("hello")
    h1 := hash(s)
    error += if h1 != 0zu { 0 } else { 1 }

    // Same string should produce same hash
    s2 := String.from_cstr("hello")
    h2 := hash(s2)
    error += if h1 == h2 { 0 } else { 1 }

    // Different string should (likely) produce different hash
    s3 := String.from_cstr("world")
    h3 := hash(s3)
    error += if h1 != h3 { 0 } else { 1 }

    // Via generic trait-bounded function
    h4 := compute_hash(s)
    error += if h4 == h1 { 0 } else { 1 }

    error
}
