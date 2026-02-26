#module main
#import <Str.tl>
#import <Array.tl>
#import <stdio.tl>
#import <string.tl>

// Sentinel for unwrapping Option[UInt] — any value impossible as a valid index.
_NONE: UInt := 9999u

// -- Construction and basic queries --

test_empty() {
    error := 0
    s     := Str.empty()
    if Str.len(s) != 0u { error += 1 }
    if !Str.is_empty(s) { error += 1 }
    error
}

test_from_cstr_small() {
    error := 0
    s     := Str.from_cstr(c"hello")
    if Str.len(s) != 5u { error += 1 }
    if Str.is_empty(s) { error += 1 }
    Str.free(s.&)
    error
}

test_from_cstr_big() {
    error := 0
    s     := Str.from_cstr(c"this is a longer string!!")
    if Str.len(s) != 25u { error += 1 }
    if Str.is_empty(s) { error += 1 }
    Str.free(s.&)
    error
}

test_from_bytes() {
    error      := 0
    p: CString := c"hello"
    s          := Str.from_bytes(p, 5u)
    if Str.len(s) != 5u { error += 1 }
    Str.free(s.&)

    // With embedded null
    p2: CString := c"ab\0cd"
    s2          := Str.from_bytes(p2, 5u)
    if Str.len(s2) != 5u { error += 1 }
    Str.free(s2.&)

    // n=0 -> empty
    s3 := Str.from_bytes(p, 0u)
    if Str.len(s3) != 0u { error += 1 }
    if !Str.is_empty(s3) { error += 1 }
    Str.free(s3.&)

    error
}

test_copy_small() {
    error := 0
    s     := Str.from_cstr(c"hello")
    c     := Str.copy(s)
    if Str.len(c) != 5u { error += 1 }
    // Should be independent
    Str.free(s.&)
    if Str.len(c) != 5u { error += 1 }
    Str.free(c.&)
    error
}

test_copy_big() {
    error := 0
    s     := Str.from_cstr(c"this is a longer string!!")
    c     := Str.copy(s)
    if Str.len(c) != 25u { error += 1 }
    Str.free(s.&)
    if Str.len(c) != 25u { error += 1 }
    Str.free(c.&)
    error
}

test_copy_empty() {
    error := 0
    e     := Str.empty()
    c     := Str.copy(e)
    if Str.len(c) != 0u { error += 1 }
    if !Str.is_empty(c) { error += 1 }
    Str.free(c.&)
    error
}

// -- C string interop --

test_cstr() {
    error := 0
    s     := Str.from_cstr(c"hello")
    cs    := Str.cstr(s.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }

    s2  := Str.from_cstr(c"this is a longer string!!")
    cs2 := Str.cstr(s2.&)
    if c_strcmp(cs2, c"this is a longer string!!") != 0 { error += 1 }
    Str.free(s.&)
    Str.free(s2.&)
    error
}

// -- Comparison --

test_eq() {
    error := 0
    a     := Str.from_cstr(c"hello")
    b     := Str.from_cstr(c"hello")
    c     := Str.from_cstr(c"world")
    if !Str.eq(a, b) { error += 1 }
    if Str.eq(a, c) { error += 1 }

    // Empty strings
    e1 := Str.empty()
    e2 := Str.empty()
    if !Str.eq(e1, e2) { error += 1 }

    Str.free(a.&)
    Str.free(b.&)
    Str.free(c.&)
    error
}

test_cmp() {
    error := 0
    a     := Str.from_cstr(c"abc")
    b     := Str.from_cstr(c"abd")
    c     := Str.from_cstr(c"abc")
    if Str.cmp(a, b) >= 0 { error += 1 }
    if Str.cmp(b, a) <= 0 { error += 1 }
    if Str.cmp(a, c) != 0 { error += 1 }

    // Prefix relationship: shorter < longer when prefix matches
    d := Str.from_cstr(c"abcd")
    if Str.cmp(a, d) >= 0 { error += 1 } // "abc" < "abcd"
    if Str.cmp(d, a) <= 0 { error += 1 } // "abcd" > "abc"
    Str.free(d.&)

    // Empty vs non-empty
    e := Str.empty()
    if Str.cmp(e, a) >= 0 { error += 1 } // "" < "abc"
    if Str.cmp(a, e) <= 0 { error += 1 } // "abc" > ""

    // Empty vs empty
    e2 := Str.empty()
    if Str.cmp(e, e2) != 0 { error += 1 }

    Str.free(a.&)
    Str.free(b.&)
    Str.free(c.&)
    error
}

test_starts_with() {
    error := 0
    s     := Str.from_cstr(c"hello world")
    a     := Str.from_cstr(c"hello")
    b     := Str.from_cstr(c"world")
    e     := Str.empty()
    if !Str.starts_with(s, a) { error += 1 }
    if Str.starts_with(s, b) { error += 1 }
    if !Str.starts_with(s, e) { error += 1 }

    // Prefix longer than string
    long_p := Str.from_cstr(c"hello world and more")
    if Str.starts_with(s, long_p) { error += 1 }
    Str.free(long_p.&)

    // Exact match: prefix == string
    if !Str.starts_with(s, s) { error += 1 }

    Str.free(s.&)
    Str.free(a.&)
    Str.free(b.&)
    error
}

test_ends_with() {
    error := 0
    s     := Str.from_cstr(c"hello world")
    a     := Str.from_cstr(c"world")
    b     := Str.from_cstr(c"hello")
    e     := Str.empty()
    if !Str.ends_with(s, a) { error += 1 }
    if Str.ends_with(s, b) { error += 1 }
    if !Str.ends_with(s, e) { error += 1 }

    // Suffix longer than string
    long_s := Str.from_cstr(c"say hello world")
    if Str.ends_with(s, long_s) { error += 1 }
    Str.free(long_s.&)

    // Exact match: suffix == string
    if !Str.ends_with(s, s) { error += 1 }

    Str.free(s.&)
    Str.free(a.&)
    Str.free(b.&)
    error
}

test_contains() {
    error := 0
    s     := Str.from_cstr(c"hello world")
    a     := Str.from_cstr(c"lo wo")
    b     := Str.from_cstr(c"xyz")
    e     := Str.empty()
    if !Str.contains(s, a) { error += 1 }
    if Str.contains(s, b) { error += 1 }
    if !Str.contains(s, e) { error += 1 }

    // Needle at very start
    h := Str.from_cstr(c"hello")
    if !Str.contains(s, h) { error += 1 }
    Str.free(h.&)

    // Needle at very end
    w := Str.from_cstr(c"world")
    if !Str.contains(s, w) { error += 1 }
    Str.free(w.&)

    // Haystack == needle
    if !Str.contains(s, s) { error += 1 }

    Str.free(s.&)
    Str.free(a.&)
    Str.free(b.&)
    error
}

test_contains_byte() {
    error := 0
    s     := Str.from_cstr(c"hello")
    if !Str.contains_byte(s, 104u) { error += 1 } // 'h'
    if Str.contains_byte(s, 122u) { error += 1 }  // 'z'
    Str.free(s.&)

    // Empty string
    e := Str.empty()
    if Str.contains_byte(e, 'a') { error += 1 }

    error
}

// -- Concatenation --

test_cat_small_small() {
    error := 0
    a     := Str.from_cstr(c"hi")
    b     := Str.from_cstr(c" there")
    c     := Str.cat(a, b)
    if Str.len(c) != 8u { error += 1 }
    cs := Str.cstr(c.&)
    if c_strcmp(cs, c"hi there") != 0 { error += 1 }
    Str.free(c.&)

    // empty + x
    e  := Str.empty()
    r1 := Str.cat(e, a)
    if Str.len(r1) != 2u { error += 1 }
    cs1 := Str.cstr(r1.&)
    if c_strcmp(cs1, c"hi") != 0 { error += 1 }
    Str.free(r1.&)

    // x + empty
    r2  := Str.cat(a, e)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hi") != 0 { error += 1 }
    Str.free(r2.&)

    // empty + empty
    r3 := Str.cat(e, e)
    if Str.len(r3) != 0u { error += 1 }
    Str.free(r3.&)

    Str.free(a.&)
    Str.free(b.&)
    error
}

test_cat_to_big() {
    error := 0
    a     := Str.from_cstr(c"hello ")
    b     := Str.from_cstr(c"beautiful world!")
    c     := Str.cat(a, b)
    if Str.len(c) != 22u { error += 1 }
    cs := Str.cstr(c.&)
    if c_strcmp(cs, c"hello beautiful world!") != 0 { error += 1 }
    Str.free(a.&)
    Str.free(b.&)
    Str.free(c.&)
    error
}

test_push() {
    error := 0
    s     := Str.from_cstr(c"hello")
    b     := Str.from_cstr(c" world")
    Str.push(s.&, b)
    if Str.len(s) != 11u { error += 1 }
    cs := Str.cstr(s.&)
    if c_strcmp(cs, c"hello world") != 0 { error += 1 }
    Str.free(s.&)
    Str.free(b.&)
    error
}

test_cat_3() {
    error := 0
    a     := Str.from_cstr(c"one")
    b     := Str.from_cstr(c" two")
    c     := Str.from_cstr(c" three")
    r     := Str.cat_3(a, b, c)
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"one two three") != 0 { error += 1 }
    Str.free(a.&)
    Str.free(b.&)
    Str.free(c.&)
    Str.free(r.&)
    error
}

test_cat_4() {
    error := 0
    a     := Str.from_cstr(c"one")
    b     := Str.from_cstr(c" two")
    c     := Str.from_cstr(c" three")
    d     := Str.from_cstr(c" four")
    r     := Str.cat_4(a, b, c, d)
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"one two three four") != 0 { error += 1 }
    if Str.len(r) != 18u { error += 1 }
    Str.free(a.&)
    Str.free(b.&)
    Str.free(c.&)
    Str.free(d.&)
    Str.free(r.&)
    error
}

// -- Access and slicing --

test_byte_at() {
    error := 0
    s     := Str.from_cstr(c"abc")
    if unwrap(Str.byte_at(s, 0u), '\0') != 'a' { error += 1 } // 'a'
    if unwrap(Str.byte_at(s, 1u), '\0') != 'b' { error += 1 } // 'b'
    if unwrap(Str.byte_at(s, 2u), '\0') != 'c' { error += 1 } // 'c'
    Str.free(s.&)
    error
}

test_slice() {
    error := 0
    s     := Str.from_cstr(c"hello world")
    sub   := Str.slice(s, 6u, 11u)
    if Str.len(sub) != 5u { error += 1 }
    cs := Str.cstr(sub.&)
    if c_strcmp(cs, c"world") != 0 { error += 1 }

    // Empty slice
    e := Str.slice(s, 3u, 3u)
    if Str.len(e) != 0u { error += 1 }

    // Full string slice
    full := Str.slice(s, 0u, 11u)
    csf  := Str.cstr(full.&)
    if c_strcmp(csf, c"hello world") != 0 { error += 1 }
    Str.free(full.&)

    // Slice from start
    head := Str.slice(s, 0u, 5u)
    csh  := Str.cstr(head.&)
    if c_strcmp(csh, c"hello") != 0 { error += 1 }
    Str.free(head.&)

    Str.free(s.&)
    Str.free(sub.&)
    Str.free(e.&)
    error
}

// -- Search --

test_index_of_byte() {
    error := 0
    s     := Str.from_cstr(c"hello")
    if unwrap(Str.index_of_byte(s, 108u), _NONE) != 2u { error += 1 }    // 'l'
    if unwrap(Str.index_of_byte(s, 111u), _NONE) != 4u { error += 1 }    // 'o'
    if unwrap(Str.index_of_byte(s, 122u), _NONE) != _NONE { error += 1 } // 'z' not found

    // First byte
    if unwrap(Str.index_of_byte(s, 'h'), _NONE) != 0u { error += 1 }

    // Last byte
    if unwrap(Str.index_of_byte(s, 'o'), _NONE) != 4u { error += 1 }

    Str.free(s.&)

    // Empty string
    e := Str.empty()
    if unwrap(Str.index_of_byte(e, 'a'), _NONE) != _NONE { error += 1 }

    error
}

// -- Conversion --

test_from_int() {
    error := 0
    s     := Str.from_int(42)
    cs    := Str.cstr(s.&)
    if c_strcmp(cs, c"42") != 0 { error += 1 }
    Str.free(s.&)

    s2  := Str.from_int(-123)
    cs2 := Str.cstr(s2.&)
    if c_strcmp(cs2, c"-123") != 0 { error += 1 }
    Str.free(s2.&)

    s3  := Str.from_int(0)
    cs3 := Str.cstr(s3.&)
    if c_strcmp(cs3, c"0") != 0 { error += 1 }
    Str.free(s3.&)
    error
}

test_from_float() {
    error := 0

    // Positive
    s  := Str.from_float(3.14)
    cs := Str.cstr(s.&)
    if c_strcmp(cs, c"3.14") != 0 { error += 1 }
    Str.free(s.&)

    // Negative
    s2  := Str.from_float(-2.5)
    cs2 := Str.cstr(s2.&)
    if c_strcmp(cs2, c"-2.5") != 0 { error += 1 }
    Str.free(s2.&)

    // Zero
    s3  := Str.from_float(0.0)
    cs3 := Str.cstr(s3.&)
    if c_strcmp(cs3, c"0") != 0 { error += 1 }
    Str.free(s3.&)

    // Integer-valued float
    s4  := Str.from_float(42.0)
    cs4 := Str.cstr(s4.&)
    if c_strcmp(cs4, c"42") != 0 { error += 1 }
    Str.free(s4.&)

    // Small fractional
    s5  := Str.from_float(0.1)
    cs5 := Str.cstr(s5.&)
    if c_strcmp(cs5, c"0.1") != 0 { error += 1 }
    Str.free(s5.&)

    error
}

// -- SSO boundary --

test_sso_boundary() {
    error := 0
    // 14 bytes should be small
    s14 := Str.from_cstr(c"12345678901234")
    if Str.len(s14) != 14u { error += 1 }
    if !Str._is_small(s14) { error += 1 }
    cs14 := Str.cstr(s14.&)
    if c_strcmp(cs14, c"12345678901234") != 0 { error += 1 }
    // Verify cstr() didn't corrupt the string
    if Str.len(s14) != 14u { error += 1 }
    if !Str._is_small(s14) { error += 1 }

    // 15 bytes should be big
    s15 := Str.from_cstr(c"123456789012345")
    if Str.len(s15) != 15u { error += 1 }
    if Str._is_small(s15) { error += 1 }
    cs15 := Str.cstr(s15.&)
    if c_strcmp(cs15, c"123456789012345") != 0 { error += 1 }
    Str.free(s14.&)
    Str.free(s15.&)
    error
}

// -- Replace --

test_replace_byte() {
    error := 0
    s     := Str.from_cstr(c"hello")
    r     := Str.replace_byte(s, 108u, 114u) // 'l' -> 'r'
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"herro") != 0 { error += 1 }
    Str.free(r.&)

    // No matches: byte not present
    r2  := Str.replace_byte(s, 'z', 'x')
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hello") != 0 { error += 1 }
    Str.free(r2.&)

    Str.free(s.&)

    // Empty string input
    e  := Str.empty()
    re := Str.replace_byte(e, 'a', 'b')
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    // All bytes matching
    s2  := Str.from_cstr(c"aaa")
    r3  := Str.replace_byte(s2, 'a', 'b')
    cs3 := Str.cstr(r3.&)
    if c_strcmp(cs3, c"bbb") != 0 { error += 1 }
    Str.free(r3.&)
    Str.free(s2.&)

    error
}

// -- New function tests --

test_from_byte() {
    error := 0
    s     := Str.from_byte(65u)         // 'A'
    if Str.len(s) != 1u { error += 1 }
    if unwrap(Str.byte_at(s, 0u), '\0') != 'A' { error += 1 }
    Str.free(s.&)

    s2 := Str.from_byte(122u)           // 'z'
    if Str.len(s2) != 1u { error += 1 }
    if unwrap(Str.byte_at(s2, 0u), '\0') != 'z' { error += 1 }
    Str.free(s2.&)
    error
}

test_eq_cstr() {
    error := 0
    s     := Str.from_cstr(c"hello")
    if !Str.eq_cstr(s, c"hello") { error += 1 }
    if Str.eq_cstr(s, c"world") { error += 1 }
    if Str.eq_cstr(s, c"hell") { error += 1 } // length mismatch
    Str.free(s.&)

    // Both empty
    e := Str.empty()
    if !Str.eq_cstr(e, c"") { error += 1 }
    error
}

test_index_of() {
    error := 0
    s     := Str.from_cstr(c"hello world")
    h     := Str.from_cstr(c"hello")
    w     := Str.from_cstr(c"world")
    lo    := Str.from_cstr(c"lo")
    xyz   := Str.from_cstr(c"xyz")
    e     := Str.empty()

    if unwrap(Str.index_of(s, h), _NONE) != 0u { error += 1 }      // beginning
    if unwrap(Str.index_of(s, w), _NONE) != 6u { error += 1 }      // end
    if unwrap(Str.index_of(s, lo), _NONE) != 3u { error += 1 }     // middle
    if unwrap(Str.index_of(s, xyz), _NONE) != _NONE { error += 1 } // not found
    if unwrap(Str.index_of(s, e), _NONE) != 0u { error += 1 }      // empty needle -> 0

    // Needle longer than haystack
    big := Str.from_cstr(c"this is way too long")
    if unwrap(Str.index_of(h, big), _NONE) != _NONE { error += 1 }

    Str.free(s.&)
    Str.free(h.&)
    Str.free(w.&)
    Str.free(lo.&)
    Str.free(xyz.&)
    Str.free(big.&)
    error
}

test_index_of_byte_from() {
    error := 0
    s     := Str.from_cstr(c"hello")

    // Skip first 'l' at index 2, find second 'l' at index 3
    if unwrap(Str.index_of_byte_from(s, 108u, 3u), _NONE) != 3u { error += 1 }

    // Start at match position
    if unwrap(Str.index_of_byte_from(s, 108u, 2u), _NONE) != 2u { error += 1 }

    // Start past all occurrences
    if unwrap(Str.index_of_byte_from(s, 108u, 4u), _NONE) != _NONE { error += 1 }

    // Start >= len
    if unwrap(Str.index_of_byte_from(s, 108u, 5u), _NONE) != _NONE { error += 1 }
    if unwrap(Str.index_of_byte_from(s, 108u, 100u), _NONE) != _NONE { error += 1 }

    Str.free(s.&)
    error
}

test_last_index_of_byte() {
    error := 0
    s     := Str.from_cstr(c"hello")

    // Multiple 'l' - finds last at index 3
    if unwrap(Str.last_index_of_byte(s, 108u), _NONE) != 3u { error += 1 }

    // Single 'h' at index 0
    if unwrap(Str.last_index_of_byte(s, 104u), _NONE) != 0u { error += 1 }

    // Single 'o' at end
    if unwrap(Str.last_index_of_byte(s, 111u), _NONE) != 4u { error += 1 }

    // Not found
    if unwrap(Str.last_index_of_byte(s, 122u), _NONE) != _NONE { error += 1 }

    // Empty string
    e := Str.empty()
    if unwrap(Str.last_index_of_byte(e, 97u), _NONE) != _NONE { error += 1 }

    Str.free(s.&)
    error
}

test_last_index_of() {
    error := 0
    s     := Str.from_cstr(c"abcabcabc")
    abc   := Str.from_cstr(c"abc")
    e     := Str.empty()

    // Repeated pattern - finds last
    if unwrap(Str.last_index_of(s, abc), _NONE) != 6u { error += 1 }

    // Empty needle -> len(s)
    if unwrap(Str.last_index_of(s, e), _NONE) != 9u { error += 1 }

    // Not found
    xyz := Str.from_cstr(c"xyz")
    if unwrap(Str.last_index_of(s, xyz), _NONE) != _NONE { error += 1 }

    // Needle longer than haystack
    short := Str.from_cstr(c"ab")
    long  := Str.from_cstr(c"abcdef")
    if unwrap(Str.last_index_of(short, long), _NONE) != _NONE { error += 1 }
    Str.free(short.&)
    Str.free(long.&)

    // Single occurrence
    hw := Str.from_cstr(c"hello world")
    w  := Str.from_cstr(c"world")
    if unwrap(Str.last_index_of(hw, w), _NONE) != 6u { error += 1 }
    Str.free(hw.&)
    Str.free(w.&)

    Str.free(s.&)
    Str.free(abc.&)
    Str.free(xyz.&)
    error
}

test_to_upper() {
    error := 0
    s     := Str.from_cstr(c"hello")
    r     := Str.to_upper(s)
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"HELLO") != 0 { error += 1 }
    Str.free(r.&)

    // Mixed case
    s2  := Str.from_cstr(c"Hello World")
    r2  := Str.to_upper(s2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"HELLO WORLD") != 0 { error += 1 }
    Str.free(r2.&)

    // Already upper
    s3  := Str.from_cstr(c"ABC")
    r3  := Str.to_upper(s3)
    cs3 := Str.cstr(r3.&)
    if c_strcmp(cs3, c"ABC") != 0 { error += 1 }
    Str.free(r3.&)

    // Non-alpha preserved
    s4  := Str.from_cstr(c"a1b2!")
    r4  := Str.to_upper(s4)
    cs4 := Str.cstr(r4.&)
    if c_strcmp(cs4, c"A1B2!") != 0 { error += 1 }
    Str.free(r4.&)

    // Empty
    e  := Str.empty()
    re := Str.to_upper(e)
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    Str.free(s.&)
    Str.free(s2.&)
    Str.free(s3.&)
    Str.free(s4.&)
    error
}

test_to_lower() {
    error := 0
    s     := Str.from_cstr(c"HELLO")
    r     := Str.to_lower(s)
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    Str.free(r.&)

    // Mixed
    s2  := Str.from_cstr(c"Hello World")
    r2  := Str.to_lower(s2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hello world") != 0 { error += 1 }
    Str.free(r2.&)

    // Already lower
    s3  := Str.from_cstr(c"abc")
    r3  := Str.to_lower(s3)
    cs3 := Str.cstr(r3.&)
    if c_strcmp(cs3, c"abc") != 0 { error += 1 }
    Str.free(r3.&)

    // Non-alpha preserved
    s4  := Str.from_cstr(c"A1B2!")
    r4  := Str.to_lower(s4)
    cs4 := Str.cstr(r4.&)
    if c_strcmp(cs4, c"a1b2!") != 0 { error += 1 }
    Str.free(r4.&)
    Str.free(s4.&)

    // Empty
    e  := Str.empty()
    re := Str.to_lower(e)
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    Str.free(s.&)
    Str.free(s2.&)
    Str.free(s3.&)
    error
}

test_trim_left() {
    error := 0

    // Leading spaces
    s  := Str.from_cstr(c"  hello")
    r  := Str.trim_left(s)
    cs := Str.cstr(r.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(s.&)

    // No leading whitespace
    s2  := Str.from_cstr(c"hello  ")
    r2  := Str.trim_left(s2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hello  ") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(s2.&)

    // All whitespace
    s3 := Str.from_cstr(c"   ")
    r3 := Str.trim_left(s3)
    if Str.len(r3) != 0u { error += 1 }
    Str.free(r3.&)
    Str.free(s3.&)

    // Empty string
    e  := Str.empty()
    re := Str.trim_left(e)
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    error
}

test_trim_right() {
    error := 0

    // Trailing spaces
    s  := Str.from_cstr(c"hello  ")
    r  := Str.trim_right(s)
    cs := Str.cstr(r.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(s.&)

    // No trailing whitespace
    s2  := Str.from_cstr(c"  hello")
    r2  := Str.trim_right(s2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"  hello") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(s2.&)

    // All whitespace
    s3 := Str.from_cstr(c"   ")
    r3 := Str.trim_right(s3)
    if Str.len(r3) != 0u { error += 1 }
    Str.free(r3.&)
    Str.free(s3.&)

    // Empty string
    e  := Str.empty()
    re := Str.trim_right(e)
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    error
}

test_trim() {
    error := 0

    // Both sides
    s  := Str.from_cstr(c"  hello  ")
    r  := Str.trim(s)
    cs := Str.cstr(r.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(s.&)

    // No whitespace
    s2  := Str.from_cstr(c"hello")
    r2  := Str.trim(s2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hello") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(s2.&)

    // All whitespace
    s3 := Str.from_cstr(c"   ")
    r3 := Str.trim(s3)
    if Str.len(r3) != 0u { error += 1 }
    Str.free(r3.&)
    Str.free(s3.&)

    // Empty
    e  := Str.empty()
    re := Str.trim(e)
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    // Tabs and newlines
    s4  := Str.from_cstr(c"\t\nhello\r\n")
    r4  := Str.trim(s4)
    cs4 := Str.cstr(r4.&)
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    Str.free(r4.&)
    Str.free(s4.&)

    error
}

test_repeat() {
    error := 0

    // Basic repeat
    s  := Str.from_cstr(c"ab")
    r  := Str.repeat(s, 3u)
    cs := Str.cstr(r.&)
    if c_strcmp(cs, c"ababab") != 0 { error += 1 }
    if Str.len(r) != 6u { error += 1 }
    Str.free(r.&)
    Str.free(s.&)

    // count=0 -> empty
    s2 := Str.from_cstr(c"hello")
    r2 := Str.repeat(s2, 0u)
    if Str.len(r2) != 0u { error += 1 }
    Str.free(r2.&)

    // count=1 -> copy
    r3  := Str.repeat(s2, 1u)
    cs3 := Str.cstr(r3.&)
    if c_strcmp(cs3, c"hello") != 0 { error += 1 }
    Str.free(r3.&)
    Str.free(s2.&)

    // Empty string
    e  := Str.empty()
    re := Str.repeat(e, 5u)
    if Str.len(re) != 0u { error += 1 }
    Str.free(re.&)

    // SSO boundary crossing: 5 bytes * 3 = 15 > 14 (SSO max)
    s4 := Str.from_cstr(c"12345")
    r4 := Str.repeat(s4, 3u)
    if Str.len(r4) != 15u { error += 1 }
    if Str._is_small(r4) { error += 1 } // should be big
    cs4 := Str.cstr(r4.&)
    if c_strcmp(cs4, c"123451234512345") != 0 { error += 1 }
    Str.free(r4.&)
    Str.free(s4.&)

    error
}

test_replace() {
    error := 0

    // Basic replace
    s     := Str.from_cstr(c"hello world")
    old   := Str.from_cstr(c"world")
    new_s := Str.from_cstr(c"there")
    r     := Str.replace(s, old, new_s)
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"hello there") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(new_s.&)
    Str.free(old.&)
    Str.free(s.&)

    // Multiple occurrences
    s2   := Str.from_cstr(c"aaa")
    old2 := Str.from_cstr(c"a")
    new2 := Str.from_cstr(c"bb")
    r2   := Str.replace(s2, old2, new2)
    cs2  := Str.cstr(r2.&)
    if c_strcmp(cs2, c"bbbbbb") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(new2.&)
    Str.free(old2.&)
    Str.free(s2.&)

    // No match -> copy
    s3   := Str.from_cstr(c"hello")
    old3 := Str.from_cstr(c"xyz")
    new3 := Str.from_cstr(c"abc")
    r3   := Str.replace(s3, old3, new3)
    cs3  := Str.cstr(r3.&)
    if c_strcmp(cs3, c"hello") != 0 { error += 1 }
    Str.free(r3.&)
    Str.free(new3.&)
    Str.free(old3.&)
    Str.free(s3.&)

    // Replace with empty (deletion)
    s4   := Str.from_cstr(c"hello world")
    old4 := Str.from_cstr(c" world")
    new4 := Str.empty()
    r4   := Str.replace(s4, old4, new4)
    cs4  := Str.cstr(r4.&)
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    Str.free(r4.&)
    Str.free(old4.&)
    Str.free(s4.&)

    // Replace with longer string
    s5   := Str.from_cstr(c"a-b-c")
    old5 := Str.from_cstr(c"-")
    new5 := Str.from_cstr(c"--")
    r5   := Str.replace(s5, old5, new5)
    cs5  := Str.cstr(r5.&)
    if c_strcmp(cs5, c"a--b--c") != 0 { error += 1 }
    Str.free(r5.&)
    Str.free(new5.&)
    Str.free(old5.&)
    Str.free(s5.&)

    // Replace crossing SSO boundary: "ab-cd-ef" (8) -> "ab___cd___ef" (12+) wait...
    // "ab-cd-ef" replacing "-" with "-----" = "ab-----cd-----ef" = 16 bytes
    s6   := Str.from_cstr(c"ab-cd-ef")
    old6 := Str.from_cstr(c"-")
    new6 := Str.from_cstr(c"-----")
    r6   := Str.replace(s6, old6, new6)
    if Str.len(r6) != 16u { error += 1 }
    if Str._is_small(r6) { error += 1 }
    cs6 := Str.cstr(r6.&)
    if c_strcmp(cs6, c"ab-----cd-----ef") != 0 { error += 1 }
    Str.free(r6.&)
    Str.free(new6.&)
    Str.free(old6.&)
    Str.free(s6.&)

    // Replace on empty string (old longer than s -> copy)
    s7   := Str.empty()
    old7 := Str.from_cstr(c"x")
    new7 := Str.from_cstr(c"y")
    r7   := Str.replace(s7, old7, new7)
    if Str.len(r7) != 0u { error += 1 }
    Str.free(r7.&)
    Str.free(new7.&)
    Str.free(old7.&)

    error
}

test_split() {
    error := 0

    // Basic split
    s   := Str.from_cstr(c"a,b,c")
    d   := Str.from_cstr(c",")
    arr := Str.split(s, d)
    if arr.size != 3u { error += 1 }
    e0 := Array.get(arr, 0u)
    e1 := Array.get(arr, 1u)
    e2 := Array.get(arr, 2u)
    if !Str.eq_cstr(e0, c"a") { error += 1 }
    if !Str.eq_cstr(e1, c"b") { error += 1 }
    if !Str.eq_cstr(e2, c"c") { error += 1 }
    Str.free(e0.&) Str.free(e1.&) Str.free(e2.&)
    Array.free(arr.&)
    Str.free(d.&)
    Str.free(s.&)

    // Consecutive delimiters -> empty strings
    s2   := Str.from_cstr(c"a,,b")
    d2   := Str.from_cstr(c",")
    arr2 := Str.split(s2, d2)
    if arr2.size != 3u { error += 1 }
    f0 := Array.get(arr2, 0u)
    f1 := Array.get(arr2, 1u)
    f2 := Array.get(arr2, 2u)
    if !Str.eq_cstr(f0, c"a") { error += 1 }
    if !Str.is_empty(f1) { error += 1 }
    if !Str.eq_cstr(f2, c"b") { error += 1 }
    Str.free(f0.&) Str.free(f1.&) Str.free(f2.&)
    Array.free(arr2.&)
    Str.free(d2.&)
    Str.free(s2.&)

    // Trailing delimiter
    s3   := Str.from_cstr(c"a,b,")
    d3   := Str.from_cstr(c",")
    arr3 := Str.split(s3, d3)
    if arr3.size != 3u { error += 1 }
    g0 := Array.get(arr3, 0u)
    g1 := Array.get(arr3, 1u)
    g2 := Array.get(arr3, 2u)
    if !Str.eq_cstr(g0, c"a") { error += 1 }
    if !Str.eq_cstr(g1, c"b") { error += 1 }
    if !Str.is_empty(g2) { error += 1 }
    Str.free(g0.&) Str.free(g1.&) Str.free(g2.&)
    Array.free(arr3.&)
    Str.free(d3.&)
    Str.free(s3.&)

    // Empty string -> [""]
    e    := Str.empty()
    d4   := Str.from_cstr(c",")
    arr4 := Str.split(e, d4)
    if arr4.size != 1u { error += 1 }
    h0 := Array.get(arr4, 0u)
    if !Str.is_empty(h0) { error += 1 }
    Str.free(h0.&)
    Array.free(arr4.&)
    Str.free(d4.&)

    // Multi-char delimiter
    s5   := Str.from_cstr(c"one::two::three")
    d5   := Str.from_cstr(c"::")
    arr5 := Str.split(s5, d5)
    if arr5.size != 3u { error += 1 }
    i0 := Array.get(arr5, 0u)
    i1 := Array.get(arr5, 1u)
    i2 := Array.get(arr5, 2u)
    if !Str.eq_cstr(i0, c"one") { error += 1 }
    if !Str.eq_cstr(i1, c"two") { error += 1 }
    if !Str.eq_cstr(i2, c"three") { error += 1 }
    Str.free(i0.&) Str.free(i1.&) Str.free(i2.&)
    Array.free(arr5.&)
    Str.free(d5.&)
    Str.free(s5.&)

    // Delimiter not found -> single element
    s6   := Str.from_cstr(c"hello")
    d6   := Str.from_cstr(c",")
    arr6 := Str.split(s6, d6)
    if arr6.size != 1u { error += 1 }
    j0 := Array.get(arr6, 0u)
    if !Str.eq_cstr(j0, c"hello") { error += 1 }
    Str.free(j0.&)
    Array.free(arr6.&)
    Str.free(d6.&)
    Str.free(s6.&)

    // Leading delimiter
    s7   := Str.from_cstr(c",a,b")
    d7   := Str.from_cstr(c",")
    arr7 := Str.split(s7, d7)
    if arr7.size != 3u { error += 1 }
    k0 := Array.get(arr7, 0u)
    k1 := Array.get(arr7, 1u)
    k2 := Array.get(arr7, 2u)
    if !Str.is_empty(k0) { error += 1 }
    if !Str.eq_cstr(k1, c"a") { error += 1 }
    if !Str.eq_cstr(k2, c"b") { error += 1 }
    Str.free(k0.&) Str.free(k1.&) Str.free(k2.&)
    Array.free(arr7.&)
    Str.free(d7.&)
    Str.free(s7.&)

    error
}

test_join() {
    error := 0

    // Basic join
    arr := Array.empty[Str.Str]()
    a   := Str.from_cstr(c"a")
    b   := Str.from_cstr(c"b")
    c   := Str.from_cstr(c"c")
    Array.push(arr.&, a)
    Array.push(arr.&, b)
    Array.push(arr.&, c)
    sep := Str.from_cstr(c",")
    r   := Str.join(sep, arr)
    cs  := Str.cstr(r.&)
    if c_strcmp(cs, c"a,b,c") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(sep.&)
    Str.free(a.&) Str.free(b.&) Str.free(c.&)
    Array.free(arr.&)

    // Empty separator
    arr2 := Array.empty[Str.Str]()
    a2   := Str.from_cstr(c"x")
    b2   := Str.from_cstr(c"y")
    Array.push(arr2.&, a2)
    Array.push(arr2.&, b2)
    sep2 := Str.empty()
    r2   := Str.join(sep2, arr2)
    cs2  := Str.cstr(r2.&)
    if c_strcmp(cs2, c"xy") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(a2.&) Str.free(b2.&)
    Array.free(arr2.&)

    // Single element
    arr3 := Array.empty[Str.Str]()
    a3   := Str.from_cstr(c"only")
    Array.push(arr3.&, a3)
    sep3 := Str.from_cstr(c",")
    r3   := Str.join(sep3, arr3)
    cs3  := Str.cstr(r3.&)
    if c_strcmp(cs3, c"only") != 0 { error += 1 }
    Str.free(r3.&)
    Str.free(sep3.&)
    Str.free(a3.&)
    Array.free(arr3.&)

    // Empty array
    arr4 := Array.empty[Str.Str]()
    sep4 := Str.from_cstr(c",")
    r4   := Str.join(sep4, arr4)
    if Str.len(r4) != 0u { error += 1 }
    Str.free(r4.&)
    Str.free(sep4.&)
    Array.free(arr4.&)

    // Empty elements -> separators only
    arr5 := Array.empty[Str.Str]()
    e1   := Str.empty()
    e2   := Str.empty()
    e3   := Str.empty()
    Array.push(arr5.&, e1)
    Array.push(arr5.&, e2)
    Array.push(arr5.&, e3)
    sep5 := Str.from_cstr(c",")
    r5   := Str.join(sep5, arr5)
    cs5  := Str.cstr(r5.&)
    if c_strcmp(cs5, c",,") != 0 { error += 1 }
    Str.free(r5.&)
    Str.free(sep5.&)
    Array.free(arr5.&)

    // Multi-char separator
    arr6 := Array.empty[Str.Str]()
    a6   := Str.from_cstr(c"a")
    b6   := Str.from_cstr(c"b")
    c6   := Str.from_cstr(c"c")
    Array.push(arr6.&, a6)
    Array.push(arr6.&, b6)
    Array.push(arr6.&, c6)
    sep6 := Str.from_cstr(c" :: ")
    r6   := Str.join(sep6, arr6)
    cs6  := Str.cstr(r6.&)
    if c_strcmp(cs6, c"a :: b :: c") != 0 { error += 1 }
    Str.free(r6.&)
    Str.free(sep6.&)
    Str.free(a6.&) Str.free(b6.&) Str.free(c6.&)
    Array.free(arr6.&)

    // Join crossing SSO boundary (result > 14 bytes)
    arr7 := Array.empty[Str.Str]()
    a7   := Str.from_cstr(c"hello")
    b7   := Str.from_cstr(c"world")
    Array.push(arr7.&, a7)
    Array.push(arr7.&, b7)
    sep7 := Str.from_cstr(c"-----")
    r7   := Str.join(sep7, arr7)        // "hello-----world" = 15 bytes
    if Str.len(r7) != 15u { error += 1 }
    if Str._is_small(r7) { error += 1 }
    cs7 := Str.cstr(r7.&)
    if c_strcmp(cs7, c"hello-----world") != 0 { error += 1 }
    Str.free(r7.&)
    Str.free(sep7.&)
    Str.free(a7.&) Str.free(b7.&)
    Array.free(arr7.&)

    error
}

test_iter() {
    error := 0

    // Sum bytes via for loop
    s         := Str.from_cstr(c"abc")
    sum: UInt := 0u
    for b in Str s {
        wide: UInt := b
        sum = sum + wide
    }
    // 'a'=97 + 'b'=98 + 'c'=99 = 294
    if sum != 294u { error += 1 }

    // Count iterations
    count := 0
    for b in Str s {
        count = count + 1
    }
    if count != 3 { error += 1 }

    // Verify actual byte values
    i := 0
    for b in Str s {
        if i == 0 { if b != 'a' { error += 1 } }
        if i == 1 { if b != 'b' { error += 1 } }
        if i == 2 { if b != 'c' { error += 1 } }
        i = i + 1
    }

    // Verify with a longer string containing varied bytes
    s2 := Str.from_cstr(c"Hello!")
    i2 := 0
    for b in Str s2 {
        if i2 == 0 { if b != 'H' { error += 1 } }
        if i2 == 1 { if b != 'e' { error += 1 } }
        if i2 == 2 { if b != 'l' { error += 1 } }
        if i2 == 3 { if b != 'l' { error += 1 } }
        if i2 == 4 { if b != 'o' { error += 1 } }
        if i2 == 5 { if b != '!' { error += 1 } }
        i2 = i2 + 1
    }
    if i2 != 6 { error += 1 }
    Str.free(s2.&)

    Str.free(s.&)

    // Empty string -> 0 iterations
    e      := Str.empty()
    count2 := 0
    for b in Str e {
        count2 = count2 + 1
    }
    if count2 != 0 { error += 1 }

    error
}

test_count_byte() {
    error := 0
    s     := Str.from_cstr(c"hello")

    // Multiple occurrences: 'l' appears twice
    if Str.count_byte(s, 'l') != 2u { error += 1 }

    // Single occurrence
    if Str.count_byte(s, 'h') != 1u { error += 1 }

    // Zero occurrences
    if Str.count_byte(s, 'z') != 0u { error += 1 }

    Str.free(s.&)

    // Empty string
    e := Str.empty()
    if Str.count_byte(e, 'a') != 0u { error += 1 }

    error
}

test_count() {
    error := 0

    // Non-overlapping matches: "ab" in "abababab" -> 4
    s  := Str.from_cstr(c"abababab")
    ab := Str.from_cstr(c"ab")
    if Str.count(s, ab) != 4u { error += 1 }
    Str.free(ab.&)

    // Non-overlapping: "abab" in "abababab" -> 2
    abab := Str.from_cstr(c"abab")
    if Str.count(s, abab) != 2u { error += 1 }
    Str.free(abab.&)
    Str.free(s.&)

    // Single match
    s2 := Str.from_cstr(c"hello world")
    w  := Str.from_cstr(c"world")
    if Str.count(s2, w) != 1u { error += 1 }
    Str.free(w.&)

    // No match
    xyz := Str.from_cstr(c"xyz")
    if Str.count(s2, xyz) != 0u { error += 1 }
    Str.free(xyz.&)
    Str.free(s2.&)

    // Needle longer than haystack
    short  := Str.from_cstr(c"ab")
    long_n := Str.from_cstr(c"abcdef")
    if Str.count(short, long_n) != 0u { error += 1 }
    Str.free(short.&)
    Str.free(long_n.&)

    // Needle == haystack
    s3 := Str.from_cstr(c"hello")
    if Str.count(s3, s3) != 1u { error += 1 }
    Str.free(s3.&)

    // Single-char needle matching all bytes
    s4 := Str.from_cstr(c"aaa")
    a2 := Str.from_cstr(c"a")
    if Str.count(s4, a2) != 3u { error += 1 }
    Str.free(s4.&)
    Str.free(a2.&)

    error
}

test_strip_prefix() {
    error := 0

    // Prefix present
    s  := Str.from_cstr(c"hello world")
    p  := Str.from_cstr(c"hello ")
    r  := Str.strip_prefix(s, p)
    cs := Str.cstr(r.&)
    if c_strcmp(cs, c"world") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(p.&)

    // Prefix absent -> copy
    p2  := Str.from_cstr(c"xyz")
    r2  := Str.strip_prefix(s, p2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hello world") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(p2.&)

    // Prefix == entire string -> empty
    r3 := Str.strip_prefix(s, s)
    if Str.len(r3) != 0u { error += 1 }
    Str.free(r3.&)

    Str.free(s.&)

    // Empty prefix -> copy
    s2  := Str.from_cstr(c"hello")
    e   := Str.empty()
    r4  := Str.strip_prefix(s2, e)
    cs4 := Str.cstr(r4.&)
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    Str.free(r4.&)
    Str.free(s2.&)

    error
}

test_strip_suffix() {
    error := 0

    // Suffix present
    s  := Str.from_cstr(c"hello world")
    x  := Str.from_cstr(c" world")
    r  := Str.strip_suffix(s, x)
    cs := Str.cstr(r.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    Str.free(r.&)
    Str.free(x.&)

    // Suffix absent -> copy
    x2  := Str.from_cstr(c"xyz")
    r2  := Str.strip_suffix(s, x2)
    cs2 := Str.cstr(r2.&)
    if c_strcmp(cs2, c"hello world") != 0 { error += 1 }
    Str.free(r2.&)
    Str.free(x2.&)

    // Suffix == entire string -> empty
    r3 := Str.strip_suffix(s, s)
    if Str.len(r3) != 0u { error += 1 }
    Str.free(r3.&)

    Str.free(s.&)

    // Empty suffix -> copy
    s2  := Str.from_cstr(c"hello")
    e   := Str.empty()
    r4  := Str.strip_suffix(s2, e)
    cs4 := Str.cstr(r4.&)
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    Str.free(r4.&)
    Str.free(s2.&)

    error
}

test_from_bool() {
    error := 0
    t     := Str.from_bool(true)
    cs    := Str.cstr(t.&)
    if c_strcmp(cs, c"true") != 0 { error += 1 }
    if Str.len(t) != 4u { error += 1 }
    if !Str._is_small(t) { error += 1 }
    Str.free(t.&)

    f   := Str.from_bool(false)
    cs2 := Str.cstr(f.&)
    if c_strcmp(cs2, c"false") != 0 { error += 1 }
    if Str.len(f) != 5u { error += 1 }
    if !Str._is_small(f) { error += 1 }
    Str.free(f.&)

    error
}

_check(name: CString, r: Int) -> Int {
    if r != 0 { c_printf(c"FAIL %s (%d)\n", name, r) }
    r
}

main() {
    error := 0
    error += _check(c"empty", test_empty())
    error += _check(c"from_cstr_small", test_from_cstr_small())
    error += _check(c"from_cstr_big", test_from_cstr_big())
    error += _check(c"from_bytes", test_from_bytes())
    error += _check(c"copy_small", test_copy_small())
    error += _check(c"copy_big", test_copy_big())
    error += _check(c"copy_empty", test_copy_empty())
    error += _check(c"cstr", test_cstr())
    error += _check(c"eq", test_eq())
    error += _check(c"cmp", test_cmp())
    error += _check(c"starts_with", test_starts_with())
    error += _check(c"ends_with", test_ends_with())
    error += _check(c"contains", test_contains())
    error += _check(c"contains_byte", test_contains_byte())
    error += _check(c"cat_small_small", test_cat_small_small())
    error += _check(c"cat_to_big", test_cat_to_big())
    error += _check(c"push", test_push())
    error += _check(c"cat_3", test_cat_3())
    error += _check(c"cat_4", test_cat_4())
    error += _check(c"byte_at", test_byte_at())
    error += _check(c"slice", test_slice())
    error += _check(c"index_of_byte", test_index_of_byte())
    error += _check(c"from_int", test_from_int())
    error += _check(c"from_float", test_from_float())
    error += _check(c"sso_boundary", test_sso_boundary())
    error += _check(c"replace_byte", test_replace_byte())
    error += _check(c"from_byte", test_from_byte())
    error += _check(c"eq_cstr", test_eq_cstr())
    error += _check(c"index_of", test_index_of())
    error += _check(c"index_of_byte_from", test_index_of_byte_from())
    error += _check(c"last_index_of_byte", test_last_index_of_byte())
    error += _check(c"last_index_of", test_last_index_of())
    error += _check(c"to_upper", test_to_upper())
    error += _check(c"to_lower", test_to_lower())
    error += _check(c"trim_left", test_trim_left())
    error += _check(c"trim_right", test_trim_right())
    error += _check(c"trim", test_trim())
    error += _check(c"repeat", test_repeat())
    error += _check(c"replace", test_replace())
    error += _check(c"split", test_split())
    error += _check(c"join", test_join())
    error += _check(c"iter", test_iter())
    error += _check(c"count_byte", test_count_byte())
    error += _check(c"count", test_count())
    error += _check(c"strip_prefix", test_strip_prefix())
    error += _check(c"strip_suffix", test_strip_suffix())
    error += _check(c"from_bool", test_from_bool())
    result: CInt := error
    result
}
