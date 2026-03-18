#module main
#import <String.tl>
#import <Array.tl>
#import <cstdio.tl>
#import <cstring.tl>

// Sentinel for unwrapping Option[CSize] — any value impossible as a valid index.
_NONE: CSize := 9999zu
// Null byte sentinel for unwrapping Option[Byte].
_NUL: Byte := 0u
// Null char sentinel
_NUL_CHAR: CChar := '\0'

// -- Construction and basic queries --

test_empty() {
    error := 0
    s     := String.empty()
    if String.len(s) != 0zu { error += 1 }
    if !String.is_empty(s) { error += 1 }
    error
}

test_from_cstr_small() {
    error := 0
    s     := String.from_cstr("hello")
    if String.len(s) != 5zu { error += 1 }
    if String.is_empty(s) { error += 1 }
    String.free(s.&)
    error
}

test_from_cstr_big() {
    error := 0
    s     := String.from_cstr("this is a longer string!!")
    if String.len(s) != 25zu { error += 1 }
    if String.is_empty(s) { error += 1 }
    String.free(s.&)
    error
}

test_from_bytes() {
    error      := 0
    p: CString := "hello"
    s          := String.from_bytes(p, 5zu)
    if String.len(s) != 5zu { error += 1 }
    String.free(s.&)

    // With embedded null
    p2: CString := "ab\0cd"
    s2          := String.from_bytes(p2, 5zu)
    if String.len(s2) != 5zu { error += 1 }
    String.free(s2.&)

    // n=0 -> empty
    s3 := String.from_bytes(p, 0zu)
    if String.len(s3) != 0zu { error += 1 }
    if !String.is_empty(s3) { error += 1 }
    String.free(s3.&)

    error
}

test_copy_small() {
    error := 0
    s     := String.from_cstr("hello")
    c     := String.copy(s)
    if String.len(c) != 5zu { error += 1 }
    // Should be independent
    String.free(s.&)
    if String.len(c) != 5zu { error += 1 }
    String.free(c.&)
    error
}

test_copy_big() {
    error := 0
    s     := String.from_cstr("this is a longer string!!")
    c     := String.copy(s)
    if String.len(c) != 25zu { error += 1 }
    String.free(s.&)
    if String.len(c) != 25zu { error += 1 }
    String.free(c.&)
    error
}

test_copy_empty() {
    error := 0
    e     := String.empty()
    c     := String.copy(e)
    if String.len(c) != 0zu { error += 1 }
    if !String.is_empty(c) { error += 1 }
    String.free(c.&)
    error
}

// -- C string interop --

test_cstr() {
    error := 0
    s     := String.from_cstr("hello")
    cs    := String.cstr(s.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }

    s2  := String.from_cstr("this is a longer string!!")
    cs2 := String.cstr(s2.&)
    if c_strcmp(cs2, "this is a longer string!!") != 0 { error += 1 }
    String.free(s.&)
    String.free(s2.&)
    error
}

// -- Comparison --

test_eq() {
    error := 0
    a     := String.from_cstr("hello")
    b     := String.from_cstr("hello")
    c     := String.from_cstr("world")
    if !String.eq(a, b) { error += 1 }
    if String.eq(a, c) { error += 1 }

    // Empty strings
    e1 := String.empty()
    e2 := String.empty()
    if !String.eq(e1, e2) { error += 1 }

    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    error
}

test_cmp() {
    error := 0
    a     := String.from_cstr("abc")
    b     := String.from_cstr("abd")
    c     := String.from_cstr("abc")
    if String.cmp(a, b) >= 0 { error += 1 }
    if String.cmp(b, a) <= 0 { error += 1 }
    if String.cmp(a, c) != 0 { error += 1 }

    // Prefix relationship: shorter < longer when prefix matches
    d := String.from_cstr("abcd")
    if String.cmp(a, d) >= 0 { error += 1 } // "abc" < "abcd"
    if String.cmp(d, a) <= 0 { error += 1 } // "abcd" > "abc"
    String.free(d.&)

    // Empty vs non-empty
    e := String.empty()
    if String.cmp(e, a) >= 0 { error += 1 } // "" < "abc"
    if String.cmp(a, e) <= 0 { error += 1 } // "abc" > ""

    // Empty vs empty
    e2 := String.empty()
    if String.cmp(e, e2) != 0 { error += 1 }

    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    error
}

test_starts_with() {
    error := 0
    s     := String.from_cstr("hello world")
    a     := String.from_cstr("hello")
    b     := String.from_cstr("world")
    e     := String.empty()
    if !String.starts_with(s, a) { error += 1 }
    if String.starts_with(s, b) { error += 1 }
    if !String.starts_with(s, e) { error += 1 }

    // Prefix longer than string
    long_p := String.from_cstr("hello world and more")
    if String.starts_with(s, long_p) { error += 1 }
    String.free(long_p.&)

    // Exact match: prefix == string
    if !String.starts_with(s, s) { error += 1 }

    String.free(s.&)
    String.free(a.&)
    String.free(b.&)
    error
}

test_ends_with() {
    error := 0
    s     := String.from_cstr("hello world")
    a     := String.from_cstr("world")
    b     := String.from_cstr("hello")
    e     := String.empty()
    if !String.ends_with(s, a) { error += 1 }
    if String.ends_with(s, b) { error += 1 }
    if !String.ends_with(s, e) { error += 1 }

    // Suffix longer than string
    long_s := String.from_cstr("say hello world")
    if String.ends_with(s, long_s) { error += 1 }
    String.free(long_s.&)

    // Exact match: suffix == string
    if !String.ends_with(s, s) { error += 1 }

    String.free(s.&)
    String.free(a.&)
    String.free(b.&)
    error
}

test_contains() {
    error := 0
    s     := String.from_cstr("hello world")
    a     := String.from_cstr("lo wo")
    b     := String.from_cstr("xyz")
    e     := String.empty()
    if !String.contains(s, a) { error += 1 }
    if String.contains(s, b) { error += 1 }
    if !String.contains(s, e) { error += 1 }

    // Needle at very start
    h := String.from_cstr("hello")
    if !String.contains(s, h) { error += 1 }
    String.free(h.&)

    // Needle at very end
    w := String.from_cstr("world")
    if !String.contains(s, w) { error += 1 }
    String.free(w.&)

    // Haystack == needle
    if !String.contains(s, s) { error += 1 }

    String.free(s.&)
    String.free(a.&)
    String.free(b.&)
    error
}

test_contains_char() {
    error := 0
    s     := String.from_cstr("hello")
    if !String.contains_char(s, 'h') { error += 1 }
    if String.contains_char(s, 'z') { error += 1 }
    String.free(s.&)

    // Empty string
    e := String.empty()
    if String.contains_char(e, 'a') { error += 1 }

    error
}

// -- Concatenation --

test_cat_small_small() {
    error := 0
    a     := String.from_cstr("hi")
    b     := String.from_cstr(" there")
    c     := String.cat(a, b)
    if String.len(c) != 8zu { error += 1 }
    cs := String.cstr(c.&)
    if c_strcmp(cs, "hi there") != 0 { error += 1 }
    String.free(c.&)

    // empty + x
    e  := String.empty()
    r1 := String.cat(e, a)
    if String.len(r1) != 2zu { error += 1 }
    cs1 := String.cstr(r1.&)
    if c_strcmp(cs1, "hi") != 0 { error += 1 }
    String.free(r1.&)

    // x + empty
    r2  := String.cat(a, e)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hi") != 0 { error += 1 }
    String.free(r2.&)

    // empty + empty
    r3 := String.cat(e, e)
    if String.len(r3) != 0zu { error += 1 }
    String.free(r3.&)

    String.free(a.&)
    String.free(b.&)
    error
}

test_cat_to_big() {
    error := 0
    a     := String.from_cstr("hello ")
    b     := String.from_cstr("beautiful world!")
    c     := String.cat(a, b)
    if String.len(c) != 22zu { error += 1 }
    cs := String.cstr(c.&)
    if c_strcmp(cs, "hello beautiful world!") != 0 { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    error
}

test_push() {
    error := 0
    s     := String.from_cstr("hello")
    b     := String.from_cstr(" world")
    String.push(s.&, b)
    if String.len(s) != 11zu { error += 1 }
    cs := String.cstr(s.&)
    if c_strcmp(cs, "hello world") != 0 { error += 1 }
    String.free(s.&)
    String.free(b.&)
    error
}

test_cat_3() {
    error := 0
    a     := String.from_cstr("one")
    b     := String.from_cstr(" two")
    c     := String.from_cstr(" three")
    r     := String.cat_3(a, b, c)
    cs    := String.cstr(r.&)
    if c_strcmp(cs, "one two three") != 0 { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    String.free(r.&)
    error
}

test_cat_4() {
    error := 0
    a     := String.from_cstr("one")
    b     := String.from_cstr(" two")
    c     := String.from_cstr(" three")
    d     := String.from_cstr(" four")
    r     := String.cat_4(a, b, c, d)
    cs    := String.cstr(r.&)
    if c_strcmp(cs, "one two three four") != 0 { error += 1 }
    if String.len(r) != 18zu { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    String.free(d.&)
    String.free(r.&)
    error
}

// -- Access and slicing --

test_byte_at() {
    error := 0
    s     := String.from_cstr("abc")
    if (b: Some := String.byte_at(s, 0zu) else { _NUL }, b.v) != 97u { error += 1 } // 'a'
    if (b: Some := String.byte_at(s, 1zu) else { _NUL }, b.v) != 98u { error += 1 } // 'b'
    if (b: Some := String.byte_at(s, 2zu) else { _NUL }, b.v) != 99u { error += 1 } // 'c'
    String.free(s.&)
    error
}

test_char_at() {
    error := 0
    s     := String.from_cstr("abc")
    if (c: Some := String.char_at(s, 0zu) else { _NUL_CHAR }, c.v) != 'a' { error += 1 }
    if (c: Some := String.char_at(s, 1zu) else { _NUL_CHAR }, c.v) != 'b' { error += 1 }
    if (c: Some := String.char_at(s, 2zu) else { _NUL_CHAR }, c.v) != 'c' { error += 1 }
    String.free(s.&)
    error
}

test_slice() {
    error := 0
    s     := String.from_cstr("hello world")
    sub   := String.slice(s, 6zu, 11zu)
    if String.len(sub) != 5zu { error += 1 }
    cs := String.cstr(sub.&)
    if c_strcmp(cs, "world") != 0 { error += 1 }

    // Empty slice
    e := String.slice(s, 3zu, 3zu)
    if String.len(e) != 0zu { error += 1 }

    // Full string slice
    full := String.slice(s, 0zu, 11zu)
    csf  := String.cstr(full.&)
    if c_strcmp(csf, "hello world") != 0 { error += 1 }
    String.free(full.&)

    // Slice from start
    head := String.slice(s, 0zu, 5zu)
    csh  := String.cstr(head.&)
    if c_strcmp(csh, "hello") != 0 { error += 1 }
    String.free(head.&)

    String.free(s.&)
    String.free(sub.&)
    String.free(e.&)
    error
}

// -- Search --

test_index_of_char() {
    error := 0
    s     := String.from_cstr("hello")
    if (r: Some := String.index_of_char(s, 'l') else { _NONE }, r.v) != 2zu { error += 1 }
    if (r: Some := String.index_of_char(s, 'o') else { _NONE }, r.v) != 4zu { error += 1 }
    error += when String.index_of_char(s, 'z') { s: Some { 1 } else { 0 } }

    // First byte
    if (r: Some := String.index_of_char(s, 'h') else { _NONE }, r.v) != 0zu { error += 1 }

    // Last byte
    if (r: Some := String.index_of_char(s, 'o') else { _NONE }, r.v) != 4zu { error += 1 }

    String.free(s.&)

    // Empty string
    e := String.empty()
    error += when String.index_of_char(e, 'a') { s: Some { 1 } else { 0 } }

    error
}

// -- Conversion --

test_from_int() {
    error := 0
    s     := String.from_int(42)
    cs    := String.cstr(s.&)
    if c_strcmp(cs, "42") != 0 { error += 1 }
    String.free(s.&)

    s2  := String.from_int(-123)
    cs2 := String.cstr(s2.&)
    if c_strcmp(cs2, "-123") != 0 { error += 1 }
    String.free(s2.&)

    s3  := String.from_int(0)
    cs3 := String.cstr(s3.&)
    if c_strcmp(cs3, "0") != 0 { error += 1 }
    String.free(s3.&)
    error
}

test_from_float() {
    error := 0

    // Positive
    s  := String.from_float(3.14)
    cs := String.cstr(s.&)
    if c_strcmp(cs, "3.14") != 0 { error += 1 }
    String.free(s.&)

    // Negative
    s2  := String.from_float(-2.5)
    cs2 := String.cstr(s2.&)
    if c_strcmp(cs2, "-2.5") != 0 { error += 1 }
    String.free(s2.&)

    // Zero
    s3  := String.from_float(0.0)
    cs3 := String.cstr(s3.&)
    if c_strcmp(cs3, "0") != 0 { error += 1 }
    String.free(s3.&)

    // Integer-valued float
    s4  := String.from_float(42.0)
    cs4 := String.cstr(s4.&)
    if c_strcmp(cs4, "42") != 0 { error += 1 }
    String.free(s4.&)

    // Small fractional
    s5  := String.from_float(0.1)
    cs5 := String.cstr(s5.&)
    if c_strcmp(cs5, "0.1") != 0 { error += 1 }
    String.free(s5.&)

    error
}

// -- SSO boundary --

test_sso_boundary() {
    error := 0
    // 14 bytes should be small
    s14 := String.from_cstr("12345678901234")
    if String.len(s14) != 14zu { error += 1 }
    if !String._is_small(s14) { error += 1 }
    cs14 := String.cstr(s14.&)
    if c_strcmp(cs14, "12345678901234") != 0 { error += 1 }
    // Verify cstr() didn't corrupt the string
    if String.len(s14) != 14zu { error += 1 }
    if !String._is_small(s14) { error += 1 }

    // 15 bytes should be big
    s15 := String.from_cstr("123456789012345")
    if String.len(s15) != 15zu { error += 1 }
    if String._is_small(s15) { error += 1 }
    cs15 := String.cstr(s15.&)
    if c_strcmp(cs15, "123456789012345") != 0 { error += 1 }
    String.free(s14.&)
    String.free(s15.&)
    error
}

// -- Replace --

test_replace_byte() {
    error := 0
    s     := String.from_cstr("hello")
    r     := String.replace_byte(s, 'l', 'r')
    cs    := String.cstr(r.&)
    if c_strcmp(cs, "herro") != 0 { error += 1 }
    String.free(r.&)

    // No matches: byte not present
    r2  := String.replace_byte(s, 'z', 'x')
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hello") != 0 { error += 1 }
    String.free(r2.&)

    String.free(s.&)

    // Empty string input
    e  := String.empty()
    re := String.replace_byte(e, 'a', 'b')
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    // All bytes matching
    s2  := String.from_cstr("aaa")
    r3  := String.replace_byte(s2, 'a', 'b')
    cs3 := String.cstr(r3.&)
    if c_strcmp(cs3, "bbb") != 0 { error += 1 }
    String.free(r3.&)
    String.free(s2.&)

    error
}

// -- New function tests --

test_from_byte() {
    error := 0
    s     := String.from_byte(65u)                                // 'A'
    if String.len(s) != 1zu { error += 1 }
    if (b: Some := String.byte_at(s, 0zu) else { _NUL }, b.v) != 65u { error += 1 } // 'A'
    String.free(s.&)

    s2 := String.from_byte(122u)                                    // 'z'
    if String.len(s2) != 1zu { error += 1 }
    if (b: Some := String.byte_at(s2, 0zu) else { _NUL }, b.v) != 122u { error += 1 } // 'z'
    String.free(s2.&)
    error
}

test_eq_cstr() {
    error := 0
    s     := String.from_cstr("hello")
    if !String.eq_cstr(s, "hello") { error += 1 }
    if String.eq_cstr(s, "world") { error += 1 }
    if String.eq_cstr(s, "hell") { error += 1 } // length mismatch
    String.free(s.&)

    // Both empty
    e := String.empty()
    if !String.eq_cstr(e, "") { error += 1 }
    error
}

test_index_of() {
    error := 0
    s     := String.from_cstr("hello world")
    h     := String.from_cstr("hello")
    w     := String.from_cstr("world")
    lo    := String.from_cstr("lo")
    xyz   := String.from_cstr("xyz")
    e     := String.empty()

    if (r: Some := String.index_of(s, h) else { _NONE }, r.v) != 0zu { error += 1 }     // beginning
    if (r: Some := String.index_of(s, w) else { _NONE }, r.v) != 6zu { error += 1 }     // end
    if (r: Some := String.index_of(s, lo) else { _NONE }, r.v) != 3zu { error += 1 }    // middle
    error += when String.index_of(s, xyz) { s: Some { 1 } else { 0 } }                  // not found
    if (r: Some := String.index_of(s, e) else { _NONE }, r.v) != 0zu { error += 1 }     // empty needle -> 0

    // Needle longer than haystack
    big := String.from_cstr("this is way too long")
    error += when String.index_of(h, big) { s: Some { 1 } else { 0 } }

    String.free(s.&)
    String.free(h.&)
    String.free(w.&)
    String.free(lo.&)
    String.free(xyz.&)
    String.free(big.&)
    error
}

test_index_of_char_from() {
    error := 0
    s     := String.from_cstr("hello")

    // Skip first 'l' at index 2, find second 'l' at index 3
    if (r: Some := String.index_of_char_from(s, 'l', 3zu) else { _NONE }, r.v) != 3zu { error += 1 }

    // Start at match position
    if (r: Some := String.index_of_char_from(s, 'l', 2zu) else { _NONE }, r.v) != 2zu { error += 1 }

    // Start past all occurrences
    error += when String.index_of_char_from(s, 'l', 4zu) { s: Some { 1 } else { 0 } }

    // Start >= len
    error += when String.index_of_char_from(s, 'l', 5zu) { s: Some { 1 } else { 0 } }
    error += when String.index_of_char_from(s, 'l', 100zu) { s: Some { 1 } else { 0 } }

    String.free(s.&)
    error
}

test_last_index_of_char() {
    error := 0
    s     := String.from_cstr("hello")

    // Multiple 'l' - finds last at index 3
    if (r: Some := String.last_index_of_char(s, 'l') else { _NONE }, r.v) != 3zu { error += 1 }

    // Single 'h' at index 0
    if (r: Some := String.last_index_of_char(s, 'h') else { _NONE }, r.v) != 0zu { error += 1 }

    // Single 'o' at end
    if (r: Some := String.last_index_of_char(s, 'o') else { _NONE }, r.v) != 4zu { error += 1 }

    // Not found
    error += when String.last_index_of_char(s, 'z') { s: Some { 1 } else { 0 } }

    // Empty string
    e := String.empty()
    error += when String.last_index_of_char(e, 'a') { s: Some { 1 } else { 0 } }

    String.free(s.&)
    error
}

test_last_index_of() {
    error := 0
    s     := String.from_cstr("abcabcabc")
    abc   := String.from_cstr("abc")
    e     := String.empty()

    // Repeated pattern - finds last
    if (r: Some := String.last_index_of(s, abc) else { _NONE }, r.v) != 6zu { error += 1 }

    // Empty needle -> len(s)
    if (r: Some := String.last_index_of(s, e) else { _NONE }, r.v) != 9zu { error += 1 }

    // Not found
    xyz := String.from_cstr("xyz")
    error += when String.last_index_of(s, xyz) { s: Some { 1 } else { 0 } }

    // Needle longer than haystack
    short := String.from_cstr("ab")
    long  := String.from_cstr("abcdef")
    error += when String.last_index_of(short, long) { s: Some { 1 } else { 0 } }
    String.free(short.&)
    String.free(long.&)

    // Single occurrence
    hw := String.from_cstr("hello world")
    w  := String.from_cstr("world")
    if (r: Some := String.last_index_of(hw, w) else { _NONE }, r.v) != 6zu { error += 1 }
    String.free(hw.&)
    String.free(w.&)

    String.free(s.&)
    String.free(abc.&)
    String.free(xyz.&)
    error
}

test_to_upper() {
    error := 0
    s     := String.from_cstr("hello")
    r     := String.to_upper(s)
    cs    := String.cstr(r.&)
    if c_strcmp(cs, "HELLO") != 0 { error += 1 }
    String.free(r.&)

    // Mixed case
    s2  := String.from_cstr("Hello World")
    r2  := String.to_upper(s2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "HELLO WORLD") != 0 { error += 1 }
    String.free(r2.&)

    // Already upper
    s3  := String.from_cstr("ABC")
    r3  := String.to_upper(s3)
    cs3 := String.cstr(r3.&)
    if c_strcmp(cs3, "ABC") != 0 { error += 1 }
    String.free(r3.&)

    // Non-alpha preserved
    s4  := String.from_cstr("a1b2!")
    r4  := String.to_upper(s4)
    cs4 := String.cstr(r4.&)
    if c_strcmp(cs4, "A1B2!") != 0 { error += 1 }
    String.free(r4.&)

    // Empty
    e  := String.empty()
    re := String.to_upper(e)
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    String.free(s.&)
    String.free(s2.&)
    String.free(s3.&)
    String.free(s4.&)
    error
}

test_to_lower() {
    error := 0
    s     := String.from_cstr("HELLO")
    r     := String.to_lower(s)
    cs    := String.cstr(r.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }
    String.free(r.&)

    // Mixed
    s2  := String.from_cstr("Hello World")
    r2  := String.to_lower(s2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hello world") != 0 { error += 1 }
    String.free(r2.&)

    // Already lower
    s3  := String.from_cstr("abc")
    r3  := String.to_lower(s3)
    cs3 := String.cstr(r3.&)
    if c_strcmp(cs3, "abc") != 0 { error += 1 }
    String.free(r3.&)

    // Non-alpha preserved
    s4  := String.from_cstr("A1B2!")
    r4  := String.to_lower(s4)
    cs4 := String.cstr(r4.&)
    if c_strcmp(cs4, "a1b2!") != 0 { error += 1 }
    String.free(r4.&)
    String.free(s4.&)

    // Empty
    e  := String.empty()
    re := String.to_lower(e)
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    String.free(s.&)
    String.free(s2.&)
    String.free(s3.&)
    error
}

test_trim_left() {
    error := 0

    // Leading spaces
    s  := String.from_cstr("  hello")
    r  := String.trim_left(s)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }
    String.free(r.&)
    String.free(s.&)

    // No leading whitespace
    s2  := String.from_cstr("hello  ")
    r2  := String.trim_left(s2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hello  ") != 0 { error += 1 }
    String.free(r2.&)
    String.free(s2.&)

    // All whitespace
    s3 := String.from_cstr("   ")
    r3 := String.trim_left(s3)
    if String.len(r3) != 0zu { error += 1 }
    String.free(r3.&)
    String.free(s3.&)

    // Empty string
    e  := String.empty()
    re := String.trim_left(e)
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    error
}

test_trim_right() {
    error := 0

    // Trailing spaces
    s  := String.from_cstr("hello  ")
    r  := String.trim_right(s)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }
    String.free(r.&)
    String.free(s.&)

    // No trailing whitespace
    s2  := String.from_cstr("  hello")
    r2  := String.trim_right(s2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "  hello") != 0 { error += 1 }
    String.free(r2.&)
    String.free(s2.&)

    // All whitespace
    s3 := String.from_cstr("   ")
    r3 := String.trim_right(s3)
    if String.len(r3) != 0zu { error += 1 }
    String.free(r3.&)
    String.free(s3.&)

    // Empty string
    e  := String.empty()
    re := String.trim_right(e)
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    error
}

test_trim() {
    error := 0

    // Both sides
    s  := String.from_cstr("  hello  ")
    r  := String.trim(s)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }
    String.free(r.&)
    String.free(s.&)

    // No whitespace
    s2  := String.from_cstr("hello")
    r2  := String.trim(s2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hello") != 0 { error += 1 }
    String.free(r2.&)
    String.free(s2.&)

    // All whitespace
    s3 := String.from_cstr("   ")
    r3 := String.trim(s3)
    if String.len(r3) != 0zu { error += 1 }
    String.free(r3.&)
    String.free(s3.&)

    // Empty
    e  := String.empty()
    re := String.trim(e)
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    // Tabs and newlines
    s4  := String.from_cstr("\t\nhello\r\n")
    r4  := String.trim(s4)
    cs4 := String.cstr(r4.&)
    if c_strcmp(cs4, "hello") != 0 { error += 1 }
    String.free(r4.&)
    String.free(s4.&)

    error
}

test_repeat() {
    error := 0

    // Basic repeat
    s  := String.from_cstr("ab")
    r  := String.repeat(s, 3zu)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "ababab") != 0 { error += 1 }
    if String.len(r) != 6zu { error += 1 }
    String.free(r.&)
    String.free(s.&)

    // count=0 -> empty
    s2 := String.from_cstr("hello")
    r2 := String.repeat(s2, 0zu)
    if String.len(r2) != 0zu { error += 1 }
    String.free(r2.&)

    // count=1 -> copy
    r3  := String.repeat(s2, 1zu)
    cs3 := String.cstr(r3.&)
    if c_strcmp(cs3, "hello") != 0 { error += 1 }
    String.free(r3.&)
    String.free(s2.&)

    // Empty string
    e  := String.empty()
    re := String.repeat(e, 5zu)
    if String.len(re) != 0zu { error += 1 }
    String.free(re.&)

    // SSO boundary crossing: 5 bytes * 3 = 15 > 14 (SSO max)
    s4 := String.from_cstr("12345")
    r4 := String.repeat(s4, 3zu)
    if String.len(r4) != 15zu { error += 1 }
    if String._is_small(r4) { error += 1 } // should be big
    cs4 := String.cstr(r4.&)
    if c_strcmp(cs4, "123451234512345") != 0 { error += 1 }
    String.free(r4.&)
    String.free(s4.&)

    error
}

test_replace() {
    error := 0

    // Basic replace
    s     := String.from_cstr("hello world")
    old   := String.from_cstr("world")
    new_s := String.from_cstr("there")
    r     := String.replace(s, old, new_s)
    cs    := String.cstr(r.&)
    if c_strcmp(cs, "hello there") != 0 { error += 1 }
    String.free(r.&)
    String.free(new_s.&)
    String.free(old.&)
    String.free(s.&)

    // Multiple occurrences
    s2   := String.from_cstr("aaa")
    old2 := String.from_cstr("a")
    new2 := String.from_cstr("bb")
    r2   := String.replace(s2, old2, new2)
    cs2  := String.cstr(r2.&)
    if c_strcmp(cs2, "bbbbbb") != 0 { error += 1 }
    String.free(r2.&)
    String.free(new2.&)
    String.free(old2.&)
    String.free(s2.&)

    // No match -> copy
    s3   := String.from_cstr("hello")
    old3 := String.from_cstr("xyz")
    new3 := String.from_cstr("abc")
    r3   := String.replace(s3, old3, new3)
    cs3  := String.cstr(r3.&)
    if c_strcmp(cs3, "hello") != 0 { error += 1 }
    String.free(r3.&)
    String.free(new3.&)
    String.free(old3.&)
    String.free(s3.&)

    // Replace with empty (deletion)
    s4   := String.from_cstr("hello world")
    old4 := String.from_cstr(" world")
    new4 := String.empty()
    r4   := String.replace(s4, old4, new4)
    cs4  := String.cstr(r4.&)
    if c_strcmp(cs4, "hello") != 0 { error += 1 }
    String.free(r4.&)
    String.free(old4.&)
    String.free(s4.&)

    // Replace with longer string
    s5   := String.from_cstr("a-b-c")
    old5 := String.from_cstr("-")
    new5 := String.from_cstr("--")
    r5   := String.replace(s5, old5, new5)
    cs5  := String.cstr(r5.&)
    if c_strcmp(cs5, "a--b--c") != 0 { error += 1 }
    String.free(r5.&)
    String.free(new5.&)
    String.free(old5.&)
    String.free(s5.&)

    // Replace crossing SSO boundary: "ab-cd-ef" (8) -> "ab___cd___ef" (12+) wait...
    // "ab-cd-ef" replacing "-" with "-----" = "ab-----cd-----ef" = 16 bytes
    s6   := String.from_cstr("ab-cd-ef")
    old6 := String.from_cstr("-")
    new6 := String.from_cstr("-----")
    r6   := String.replace(s6, old6, new6)
    if String.len(r6) != 16zu { error += 1 }
    if String._is_small(r6) { error += 1 }
    cs6 := String.cstr(r6.&)
    if c_strcmp(cs6, "ab-----cd-----ef") != 0 { error += 1 }
    String.free(r6.&)
    String.free(new6.&)
    String.free(old6.&)
    String.free(s6.&)

    // Replace on empty string (old longer than s -> copy)
    s7   := String.empty()
    old7 := String.from_cstr("x")
    new7 := String.from_cstr("y")
    r7   := String.replace(s7, old7, new7)
    if String.len(r7) != 0zu { error += 1 }
    String.free(r7.&)
    String.free(new7.&)
    String.free(old7.&)

    error
}

test_split() {
    error := 0

    // Basic split
    s   := String.from_cstr("a,b,c")
    d   := String.from_cstr(",")
    arr := String.split(s, d)
    if arr.size != 3zu { error += 1 }
    e0 := Array.get(arr, 0zu)
    e1 := Array.get(arr, 1zu)
    e2 := Array.get(arr, 2zu)
    if !String.eq_cstr(e0, "a") { error += 1 }
    if !String.eq_cstr(e1, "b") { error += 1 }
    if !String.eq_cstr(e2, "c") { error += 1 }
    String.free(e0.&) String.free(e1.&) String.free(e2.&)
    Array.free(arr.&)
    String.free(d.&)
    String.free(s.&)

    // Consecutive delimiters -> empty strings
    s2   := String.from_cstr("a,,b")
    d2   := String.from_cstr(",")
    arr2 := String.split(s2, d2)
    if arr2.size != 3zu { error += 1 }
    f0 := Array.get(arr2, 0zu)
    f1 := Array.get(arr2, 1zu)
    f2 := Array.get(arr2, 2zu)
    if !String.eq_cstr(f0, "a") { error += 1 }
    if !String.is_empty(f1) { error += 1 }
    if !String.eq_cstr(f2, "b") { error += 1 }
    String.free(f0.&) String.free(f1.&) String.free(f2.&)
    Array.free(arr2.&)
    String.free(d2.&)
    String.free(s2.&)

    // Trailing delimiter
    s3   := String.from_cstr("a,b,")
    d3   := String.from_cstr(",")
    arr3 := String.split(s3, d3)
    if arr3.size != 3zu { error += 1 }
    g0 := Array.get(arr3, 0zu)
    g1 := Array.get(arr3, 1zu)
    g2 := Array.get(arr3, 2zu)
    if !String.eq_cstr(g0, "a") { error += 1 }
    if !String.eq_cstr(g1, "b") { error += 1 }
    if !String.is_empty(g2) { error += 1 }
    String.free(g0.&) String.free(g1.&) String.free(g2.&)
    Array.free(arr3.&)
    String.free(d3.&)
    String.free(s3.&)

    // Empty string -> [""]
    e    := String.empty()
    d4   := String.from_cstr(",")
    arr4 := String.split(e, d4)
    if arr4.size != 1zu { error += 1 }
    h0 := Array.get(arr4, 0zu)
    if !String.is_empty(h0) { error += 1 }
    String.free(h0.&)
    Array.free(arr4.&)
    String.free(d4.&)

    // Multi-char delimiter
    s5   := String.from_cstr("one::two::three")
    d5   := String.from_cstr("::")
    arr5 := String.split(s5, d5)
    if arr5.size != 3zu { error += 1 }
    i0 := Array.get(arr5, 0zu)
    i1 := Array.get(arr5, 1zu)
    i2 := Array.get(arr5, 2zu)
    if !String.eq_cstr(i0, "one") { error += 1 }
    if !String.eq_cstr(i1, "two") { error += 1 }
    if !String.eq_cstr(i2, "three") { error += 1 }
    String.free(i0.&) String.free(i1.&) String.free(i2.&)
    Array.free(arr5.&)
    String.free(d5.&)
    String.free(s5.&)

    // Delimiter not found -> single element
    s6   := String.from_cstr("hello")
    d6   := String.from_cstr(",")
    arr6 := String.split(s6, d6)
    if arr6.size != 1zu { error += 1 }
    j0 := Array.get(arr6, 0zu)
    if !String.eq_cstr(j0, "hello") { error += 1 }
    String.free(j0.&)
    Array.free(arr6.&)
    String.free(d6.&)
    String.free(s6.&)

    // Leading delimiter
    s7   := String.from_cstr(",a,b")
    d7   := String.from_cstr(",")
    arr7 := String.split(s7, d7)
    if arr7.size != 3zu { error += 1 }
    k0 := Array.get(arr7, 0zu)
    k1 := Array.get(arr7, 1zu)
    k2 := Array.get(arr7, 2zu)
    if !String.is_empty(k0) { error += 1 }
    if !String.eq_cstr(k1, "a") { error += 1 }
    if !String.eq_cstr(k2, "b") { error += 1 }
    String.free(k0.&) String.free(k1.&) String.free(k2.&)
    Array.free(arr7.&)
    String.free(d7.&)
    String.free(s7.&)

    error
}

test_join() {
    error := 0

    // Basic join
    arr := Array.empty[String.String]()
    a   := String.from_cstr("a")
    b   := String.from_cstr("b")
    c   := String.from_cstr("c")
    Array.push(arr.&, a)
    Array.push(arr.&, b)
    Array.push(arr.&, c)
    sep := String.from_cstr(",")
    r   := String.join(sep, arr)
    cs  := String.cstr(r.&)
    if c_strcmp(cs, "a,b,c") != 0 { error += 1 }
    String.free(r.&)
    String.free(sep.&)
    String.free(a.&) String.free(b.&) String.free(c.&)
    Array.free(arr.&)

    // Empty separator
    arr2 := Array.empty[String.String]()
    a2   := String.from_cstr("x")
    b2   := String.from_cstr("y")
    Array.push(arr2.&, a2)
    Array.push(arr2.&, b2)
    sep2 := String.empty()
    r2   := String.join(sep2, arr2)
    cs2  := String.cstr(r2.&)
    if c_strcmp(cs2, "xy") != 0 { error += 1 }
    String.free(r2.&)
    String.free(a2.&) String.free(b2.&)
    Array.free(arr2.&)

    // Single element
    arr3 := Array.empty[String.String]()
    a3   := String.from_cstr("only")
    Array.push(arr3.&, a3)
    sep3 := String.from_cstr(",")
    r3   := String.join(sep3, arr3)
    cs3  := String.cstr(r3.&)
    if c_strcmp(cs3, "only") != 0 { error += 1 }
    String.free(r3.&)
    String.free(sep3.&)
    String.free(a3.&)
    Array.free(arr3.&)

    // Empty array
    arr4 := Array.empty[String.String]()
    sep4 := String.from_cstr(",")
    r4   := String.join(sep4, arr4)
    if String.len(r4) != 0zu { error += 1 }
    String.free(r4.&)
    String.free(sep4.&)
    Array.free(arr4.&)

    // Empty elements -> separators only
    arr5 := Array.empty[String.String]()
    e1   := String.empty()
    e2   := String.empty()
    e3   := String.empty()
    Array.push(arr5.&, e1)
    Array.push(arr5.&, e2)
    Array.push(arr5.&, e3)
    sep5 := String.from_cstr(",")
    r5   := String.join(sep5, arr5)
    cs5  := String.cstr(r5.&)
    if c_strcmp(cs5, ",,") != 0 { error += 1 }
    String.free(r5.&)
    String.free(sep5.&)
    Array.free(arr5.&)

    // Multi-char separator
    arr6 := Array.empty[String.String]()
    a6   := String.from_cstr("a")
    b6   := String.from_cstr("b")
    c6   := String.from_cstr("c")
    Array.push(arr6.&, a6)
    Array.push(arr6.&, b6)
    Array.push(arr6.&, c6)
    sep6 := String.from_cstr(" :: ")
    r6   := String.join(sep6, arr6)
    cs6  := String.cstr(r6.&)
    if c_strcmp(cs6, "a :: b :: c") != 0 { error += 1 }
    String.free(r6.&)
    String.free(sep6.&)
    String.free(a6.&) String.free(b6.&) String.free(c6.&)
    Array.free(arr6.&)

    // Join crossing SSO boundary (result > 14 bytes)
    arr7 := Array.empty[String.String]()
    a7   := String.from_cstr("hello")
    b7   := String.from_cstr("world")
    Array.push(arr7.&, a7)
    Array.push(arr7.&, b7)
    sep7 := String.from_cstr("-----")
    r7   := String.join(sep7, arr7)     // "hello-----world" = 15 bytes
    if String.len(r7) != 15zu { error += 1 }
    if String._is_small(r7) { error += 1 }
    cs7 := String.cstr(r7.&)
    if c_strcmp(cs7, "hello-----world") != 0 { error += 1 }
    String.free(r7.&)
    String.free(sep7.&)
    String.free(a7.&) String.free(b7.&)
    Array.free(arr7.&)

    error
}

test_iter() {
    error := 0

    // Sum bytes via for loop
    s          := String.from_cstr("abc")
    sum: CSize := 0zu
    for b in String s {
        wide: CSize := b
        sum = sum + wide
    }
    // 'a'=97 + 'b'=98 + 'c'=99 = 294
    if sum != 294zu { error += 1 }

    // Count iterations
    count := 0
    for b in String s {
        count = count + 1
    }
    if count != 3 { error += 1 }

    // Verify actual byte values
    i := 0
    for b in String s {
        if i == 0 { if b != 'a' { error += 1 } }
        if i == 1 { if b != 'b' { error += 1 } }
        if i == 2 { if b != 'c' { error += 1 } }
        i = i + 1
    }

    // Verify with a longer string containing varied bytes
    s2 := String.from_cstr("Hello!")
    i2 := 0
    for b in String s2 {
        if i2 == 0 { if b != 'H' { error += 1 } }
        if i2 == 1 { if b != 'e' { error += 1 } }
        if i2 == 2 { if b != 'l' { error += 1 } }
        if i2 == 3 { if b != 'l' { error += 1 } }
        if i2 == 4 { if b != 'o' { error += 1 } }
        if i2 == 5 { if b != '!' { error += 1 } }
        i2 = i2 + 1
    }
    if i2 != 6 { error += 1 }
    String.free(s2.&)

    String.free(s.&)

    // Empty string -> 0 iterations
    e      := String.empty()
    count2 := 0
    for b in String e {
        count2 = count2 + 1
    }
    if count2 != 0 { error += 1 }

    error
}

test_count_byte() {
    error := 0
    s     := String.from_cstr("hello")

    // Multiple occurrences: 'l' appears twice
    if String.count_byte(s, 'l') != 2zu { error += 1 }

    // Single occurrence
    if String.count_byte(s, 'h') != 1zu { error += 1 }

    // Zero occurrences
    if String.count_byte(s, 'z') != 0zu { error += 1 }

    String.free(s.&)

    // Empty string
    e := String.empty()
    if String.count_byte(e, 'a') != 0zu { error += 1 }

    error
}

test_count() {
    error := 0

    // Non-overlapping matches: "ab" in "abababab" -> 4
    s  := String.from_cstr("abababab")
    ab := String.from_cstr("ab")
    if String.count(s, ab) != 4zu { error += 1 }
    String.free(ab.&)

    // Non-overlapping: "abab" in "abababab" -> 2
    abab := String.from_cstr("abab")
    if String.count(s, abab) != 2zu { error += 1 }
    String.free(abab.&)
    String.free(s.&)

    // Single match
    s2 := String.from_cstr("hello world")
    w  := String.from_cstr("world")
    if String.count(s2, w) != 1zu { error += 1 }
    String.free(w.&)

    // No match
    xyz := String.from_cstr("xyz")
    if String.count(s2, xyz) != 0zu { error += 1 }
    String.free(xyz.&)
    String.free(s2.&)

    // Needle longer than haystack
    short  := String.from_cstr("ab")
    long_n := String.from_cstr("abcdef")
    if String.count(short, long_n) != 0zu { error += 1 }
    String.free(short.&)
    String.free(long_n.&)

    // Needle == haystack
    s3 := String.from_cstr("hello")
    if String.count(s3, s3) != 1zu { error += 1 }
    String.free(s3.&)

    // Single-char needle matching all bytes
    s4 := String.from_cstr("aaa")
    a2 := String.from_cstr("a")
    if String.count(s4, a2) != 3zu { error += 1 }
    String.free(s4.&)
    String.free(a2.&)

    error
}

test_strip_prefix() {
    error := 0

    // Prefix present
    s  := String.from_cstr("hello world")
    p  := String.from_cstr("hello ")
    r  := String.strip_prefix(s, p)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "world") != 0 { error += 1 }
    String.free(r.&)
    String.free(p.&)

    // Prefix absent -> copy
    p2  := String.from_cstr("xyz")
    r2  := String.strip_prefix(s, p2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hello world") != 0 { error += 1 }
    String.free(r2.&)
    String.free(p2.&)

    // Prefix == entire string -> empty
    r3 := String.strip_prefix(s, s)
    if String.len(r3) != 0zu { error += 1 }
    String.free(r3.&)

    String.free(s.&)

    // Empty prefix -> copy
    s2  := String.from_cstr("hello")
    e   := String.empty()
    r4  := String.strip_prefix(s2, e)
    cs4 := String.cstr(r4.&)
    if c_strcmp(cs4, "hello") != 0 { error += 1 }
    String.free(r4.&)
    String.free(s2.&)

    error
}

test_strip_suffix() {
    error := 0

    // Suffix present
    s  := String.from_cstr("hello world")
    x  := String.from_cstr(" world")
    r  := String.strip_suffix(s, x)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }
    String.free(r.&)
    String.free(x.&)

    // Suffix absent -> copy
    x2  := String.from_cstr("xyz")
    r2  := String.strip_suffix(s, x2)
    cs2 := String.cstr(r2.&)
    if c_strcmp(cs2, "hello world") != 0 { error += 1 }
    String.free(r2.&)
    String.free(x2.&)

    // Suffix == entire string -> empty
    r3 := String.strip_suffix(s, s)
    if String.len(r3) != 0zu { error += 1 }
    String.free(r3.&)

    String.free(s.&)

    // Empty suffix -> copy
    s2  := String.from_cstr("hello")
    e   := String.empty()
    r4  := String.strip_suffix(s2, e)
    cs4 := String.cstr(r4.&)
    if c_strcmp(cs4, "hello") != 0 { error += 1 }
    String.free(r4.&)
    String.free(s2.&)

    error
}

test_from_bool() {
    error := 0
    t     := String.from_bool(true)
    cs    := String.cstr(t.&)
    if c_strcmp(cs, "true") != 0 { error += 1 }
    if String.len(t) != 4zu { error += 1 }
    if !String._is_small(t) { error += 1 }
    String.free(t.&)

    f   := String.from_bool(false)
    cs2 := String.cstr(f.&)
    if c_strcmp(cs2, "false") != 0 { error += 1 }
    if String.len(f) != 5zu { error += 1 }
    if !String._is_small(f) { error += 1 }
    String.free(f.&)

    error
}

_check(name: CString, r: Int) -> Int {
    if r != 0 { c_printf("FAIL %s (%d)\n", name, r) }
    r
}

main() {
    error := 0
    error += _check("empty", test_empty())
    error += _check("from_cstr_small", test_from_cstr_small())
    error += _check("from_cstr_big", test_from_cstr_big())
    error += _check("from_bytes", test_from_bytes())
    error += _check("copy_small", test_copy_small())
    error += _check("copy_big", test_copy_big())
    error += _check("copy_empty", test_copy_empty())
    error += _check("cstr", test_cstr())
    error += _check("eq", test_eq())
    error += _check("cmp", test_cmp())
    error += _check("starts_with", test_starts_with())
    error += _check("ends_with", test_ends_with())
    error += _check("contains", test_contains())
    error += _check("contains_char", test_contains_char())
    error += _check("cat_small_small", test_cat_small_small())
    error += _check("cat_to_big", test_cat_to_big())
    error += _check("push", test_push())
    error += _check("cat_3", test_cat_3())
    error += _check("cat_4", test_cat_4())
    error += _check("byte_at", test_byte_at())
    error += _check("char_at", test_char_at())
    error += _check("slice", test_slice())
    error += _check("index_of_char", test_index_of_char())
    error += _check("from_int", test_from_int())
    error += _check("from_float", test_from_float())
    error += _check("sso_boundary", test_sso_boundary())
    error += _check("replace_byte", test_replace_byte())
    error += _check("from_byte", test_from_byte())
    error += _check("eq_cstr", test_eq_cstr())
    error += _check("index_of", test_index_of())
    error += _check("index_of_char_from", test_index_of_char_from())
    error += _check("last_index_of_char", test_last_index_of_char())
    error += _check("last_index_of", test_last_index_of())
    error += _check("to_upper", test_to_upper())
    error += _check("to_lower", test_to_lower())
    error += _check("trim_left", test_trim_left())
    error += _check("trim_right", test_trim_right())
    error += _check("trim", test_trim())
    error += _check("repeat", test_repeat())
    error += _check("replace", test_replace())
    error += _check("split", test_split())
    error += _check("join", test_join())
    error += _check("iter", test_iter())
    error += _check("count_byte", test_count_byte())
    error += _check("count", test_count())
    error += _check("strip_prefix", test_strip_prefix())
    error += _check("strip_suffix", test_strip_suffix())
    error += _check("from_bool", test_from_bool())
    result: CInt := error
    result
}
