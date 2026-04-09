#module main
#import <Array.tl>
#import <String.tl>
#import <cstdio.tl>
#import <cstdlib.tl>
#import <cstring.tl>
#include <float.h>  // for float limits

c_DBL_MAX_10_EXP:  CInt
c_LDBL_MAX_10_EXP: CInt

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
    if s.len() != 0zu { error += 1 }
    if !s.is_empty() { error += 1 }
    error
}

test_from_cstr_small() {
    error := 0
    s     := String.from_cstr(c"hello")
    if s.len() != 5zu { error += 1 }
    if s.is_empty() { error += 1 }
    s.free()
    error
}

test_from_cstr_big() {
    error := 0
    s     := String.from_cstr(c"this is a longer string!!")
    if s.len() != 25zu { error += 1 }
    if s.is_empty() { error += 1 }
    s.free()
    error
}

test_from_bytes() {
    error      := 0
    p: CString := c"hello"
    s          := String.from_bytes(p, 5zu)
    if s.len() != 5zu { error += 1 }
    s.free()

    // With embedded null
    p2: CString := c"ab\0cd"
    s2          := String.from_bytes(p2, 5zu)
    if s2.len() != 5zu { error += 1 }
    s2.free()

    // n=0 -> empty
    s3 := String.from_bytes(p, 0zu)
    if s3.len() != 0zu { error += 1 }
    if !s3.is_empty() { error += 1 }
    s3.free()

    error
}

test_copy_small() {
    error := 0
    s     := String.from_cstr(c"hello")
    c     := s.copy()
    if c.len() != 5zu { error += 1 }
    // Should be independent
    s.free()
    if c.len() != 5zu { error += 1 }
    c.free()
    error
}

test_copy_big() {
    error := 0
    s     := String.from_cstr(c"this is a longer string!!")
    c     := s.copy()
    if c.len() != 25zu { error += 1 }
    s.free()
    if c.len() != 25zu { error += 1 }
    c.free()
    error
}

test_copy_empty() {
    error := 0
    e     := String.empty()
    c     := e.copy()
    if c.len() != 0zu { error += 1 }
    if !c.is_empty() { error += 1 }
    c.free()
    error
}

// -- C string interop --

test_cstr() {
    error := 0
    s     := String.from_cstr(c"hello")
    cs    := s.cstr()
    if c_strcmp(cs, c"hello") != 0 { error += 1 }

    s2  := String.from_cstr(c"this is a longer string!!")
    cs2 := s2.cstr()
    if c_strcmp(cs2, c"this is a longer string!!") != 0 { error += 1 }
    s.free()
    s2.free()
    error
}

// -- Comparison --

test_eq() {
    error := 0
    a     := String.from_cstr(c"hello")
    b     := String.from_cstr(c"hello")
    c     := String.from_cstr(c"world")
    if !a.eq(b) { error += 1 }
    if a.eq(c) { error += 1 }

    // Empty strings
    e1 := String.empty()
    e2 := String.empty()
    if !e1.eq(e2) { error += 1 }

    a.free()
    b.free()
    c.free()
    error
}

test_eq_operator() {
    error := 0
    a     := "hello"
    b     := "hello"
    c     := "world"

    // == works
    if !(a == b) { error += 1 }
    if a == c { error += 1 }

    // != works
    if a != b { error += 1 }
    if !(a != c) { error += 1 }

    // Empty strings
    e1 := String.empty()
    e2 := String.empty()
    if !(e1 == e2) { error += 1 }
    if e1 != e2 { error += 1 }

    // String literal operands
    if !(a == "hello") { error += 1 }
    if a == "world" { error += 1 }

    error
}

test_cmp() {
    error := 0
    a     := String.from_cstr(c"abc")
    b     := String.from_cstr(c"abd")
    c     := String.from_cstr(c"abc")
    if a.cmp(b) >= 0 { error += 1 }
    if b.cmp(a) <= 0 { error += 1 }
    if a.cmp(c) != 0 { error += 1 }

    // Prefix relationship: shorter < longer when prefix matches
    d := String.from_cstr(c"abcd")
    if a.cmp(d) >= 0 { error += 1 }     // c"abc" < c"abcd"
    if d.cmp(a) <= 0 { error += 1 }     // c"abcd" > c"abc"
    d.free()

    // Empty vs non-empty
    e := String.empty()
    if e.cmp(a) >= 0 { error += 1 }     // c"" < c"abc"
    if a.cmp(e) <= 0 { error += 1 }     // c"abc" > c""

    // Empty vs empty
    e2 := String.empty()
    if e.cmp(e2) != 0 { error += 1 }

    a.free()
    b.free()
    c.free()
    error
}

test_starts_with() {
    error := 0
    s     := String.from_cstr(c"hello world")
    a     := String.from_cstr(c"hello")
    b     := String.from_cstr(c"world")
    e     := String.empty()
    if !s.starts_with(a) { error += 1 }
    if s.starts_with(b) { error += 1 }
    if !s.starts_with(e) { error += 1 }

    // Prefix longer than string
    long_p := String.from_cstr(c"hello world and more")
    if s.starts_with(long_p) { error += 1 }
    long_p.free()

    // Exact match: prefix == string
    if !s.starts_with(s) { error += 1 }

    s.free()
    a.free()
    b.free()
    error
}

test_ends_with() {
    error := 0
    s     := String.from_cstr(c"hello world")
    a     := String.from_cstr(c"world")
    b     := String.from_cstr(c"hello")
    e     := String.empty()
    if !s.ends_with(a) { error += 1 }
    if s.ends_with(b) { error += 1 }
    if !s.ends_with(e) { error += 1 }

    // Suffix longer than string
    long_s := String.from_cstr(c"say hello world")
    if s.ends_with(long_s) { error += 1 }
    long_s.free()

    // Exact match: suffix == string
    if !s.ends_with(s) { error += 1 }

    s.free()
    a.free()
    b.free()
    error
}

test_contains() {
    error := 0
    s     := String.from_cstr(c"hello world")
    a     := String.from_cstr(c"lo wo")
    b     := String.from_cstr(c"xyz")
    e     := String.empty()
    if !s.contains(a) { error += 1 }
    if s.contains(b) { error += 1 }
    if !s.contains(e) { error += 1 }

    // Needle at very start
    h := String.from_cstr(c"hello")
    if !s.contains(h) { error += 1 }
    h.free()

    // Needle at very end
    w := String.from_cstr(c"world")
    if !s.contains(w) { error += 1 }
    w.free()

    // Haystack == needle
    if !s.contains(s) { error += 1 }

    s.free()
    a.free()
    b.free()
    error
}

test_contains_char() {
    error := 0
    s     := String.from_cstr(c"hello")
    if !s.contains_char('h') { error += 1 }
    if s.contains_char('z') { error += 1 }
    s.free()

    // Empty string
    e := String.empty()
    if e.contains_char('a') { error += 1 }

    error
}

// -- Concatenation --

test_cat_small_small() {
    error := 0
    a     := String.from_cstr(c"hi")
    b     := String.from_cstr(c" there")
    c     := a.cat(b)
    if c.len() != 8zu { error += 1 }
    cs := c.cstr()
    if c_strcmp(cs, c"hi there") != 0 { error += 1 }
    c.free()

    // empty + x
    e  := String.empty()
    r1 := e.cat(a)
    if r1.len() != 2zu { error += 1 }
    cs1 := r1.cstr()
    if c_strcmp(cs1, c"hi") != 0 { error += 1 }
    r1.free()

    // x + empty
    r2  := a.cat(e)
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hi") != 0 { error += 1 }
    r2.free()

    // empty + empty
    r3 := e.cat(e)
    if r3.len() != 0zu { error += 1 }
    r3.free()

    a.free()
    b.free()
    error
}

test_cat_to_big() {
    error := 0
    a     := String.from_cstr(c"hello ")
    b     := String.from_cstr(c"beautiful world!")
    c     := a.cat(b)
    if c.len() != 22zu { error += 1 }
    cs := c.cstr()
    if c_strcmp(cs, c"hello beautiful world!") != 0 { error += 1 }
    a.free()
    b.free()
    c.free()
    error
}

test_push() {
    error := 0
    s     := String.from_cstr(c"hello")
    b     := String.from_cstr(c" world")
    s.push(b)
    if s.len() != 11zu { error += 1 }
    cs := s.cstr()
    if c_strcmp(cs, c"hello world") != 0 { error += 1 }
    s.free()
    b.free()
    error
}

test_cat_3() {
    error := 0
    a     := String.from_cstr(c"one")
    b     := String.from_cstr(c" two")
    c     := String.from_cstr(c" three")
    r     := a.cat_3(b, c)
    cs    := r.cstr()
    if c_strcmp(cs, c"one two three") != 0 { error += 1 }
    a.free()
    b.free()
    c.free()
    r.free()
    error
}

// -- Access and slicing --

test_byte_at() {
    error := 0
    s     := String.from_cstr(c"abc")
    if (b: Some := s.byte_at(0zu) else { _NUL }, b.value) != 97u { error += 1 } // 'a'
    if (b: Some := s.byte_at(1zu) else { _NUL }, b.value) != 98u { error += 1 } // 'b'
    if (b: Some := s.byte_at(2zu) else { _NUL }, b.value) != 99u { error += 1 } // 'c'
    s.free()
    error
}

test_char_at() {
    error := 0
    s     := String.from_cstr(c"abc")
    if (c: Some := s.char_at(0zu) else { _NUL_CHAR }, c.value) != 'a' { error += 1 }
    if (c: Some := s.char_at(1zu) else { _NUL_CHAR }, c.value) != 'b' { error += 1 }
    if (c: Some := s.char_at(2zu) else { _NUL_CHAR }, c.value) != 'c' { error += 1 }
    s.free()
    error
}

test_copy_range() {
    error := 0
    s     := String.from_cstr(c"hello world")
    sub   := s.copy(6zu, 11zu)
    if sub.len() != 5zu { error += 1 }
    cs := sub.cstr()
    if c_strcmp(cs, c"world") != 0 { error += 1 }

    // Empty copy
    e := s.copy(3zu, 3zu)
    if e.len() != 0zu { error += 1 }

    // Full string copy
    full := s.copy(0zu, 11zu)
    csf  := full.cstr()
    if c_strcmp(csf, c"hello world") != 0 { error += 1 }
    full.free()

    // Copy from start
    head := s.copy(0zu, 5zu)
    csh  := head.cstr()
    if c_strcmp(csh, c"hello") != 0 { error += 1 }
    head.free()

    s.free()
    sub.free()
    e.free()
    error
}

test_slice() {
    error := 0
    s     := String.from_cstr(c"hello world")
    // slice returns Slice[CChar], a non-owning view
    sub := s.slice(6zu, 11zu)
    if sub.size != 5zu { error += 1 }
    if sub.data.[0] != 'w' { error += 1 }
    if sub.data.[4] != 'd' { error += 1 }
    // Empty slice
    e := s.slice(3zu, 3zu)
    if e.size != 0zu { error += 1 }
    // Full string slice
    full := s.slice(0zu, 11zu)
    if full.size != 11zu { error += 1 }
    if full.data.[0] != 'h' { error += 1 }
    s.free()
    // No free needed for Slice (non-owning)
    error
}

// -- Search --

test_index_of_char() {
    error := 0
    s     := String.from_cstr(c"hello")
    if (r: Some := s.index_of_char('l') else { _NONE }, r.value) != 2zu { error += 1 }
    if (r: Some := s.index_of_char('o') else { _NONE }, r.value) != 4zu { error += 1 }
    error += when s.index_of_char('z') { s: Some { 1 } else { 0 } }

    // First byte
    if (r: Some := s.index_of_char('h') else { _NONE }, r.value) != 0zu { error += 1 }

    // Last byte
    if (r: Some := s.index_of_char('o') else { _NONE }, r.value) != 4zu { error += 1 }

    s.free()

    // Empty string
    e := String.empty()
    error += when e.index_of_char('a') { s: Some { 1 } else { 0 } }

    error
}

// -- Conversion --

test_from_int() {
    error := 0
    s     := String.from_int(42)
    cs    := s.cstr()
    if c_strcmp(cs, c"42") != 0 { error += 1 }
    s.free()

    s2  := String.from_int(-123)
    cs2 := s2.cstr()
    if c_strcmp(cs2, c"-123") != 0 { error += 1 }
    s2.free()

    s3  := String.from_int(0)
    cs3 := s3.cstr()
    if c_strcmp(cs3, c"0") != 0 { error += 1 }
    s3.free()
    error
}

test_from_float() {
    error := 0

    // Positive
    s  := String.from_float(3.14)
    cs := s.cstr()
    if c_strcmp(cs, c"3.14") != 0 { error += 1 }
    s.free()

    // Negative
    s2  := String.from_float(-2.5)
    cs2 := s2.cstr()
    if c_strcmp(cs2, c"-2.5") != 0 { error += 1 }
    s2.free()

    // Zero
    s3  := String.from_float(0.0)
    cs3 := s3.cstr()
    if c_strcmp(cs3, c"0") != 0 { error += 1 }
    s3.free()

    // Integer-valued float
    s4  := String.from_float(42.0)
    cs4 := s4.cstr()
    if c_strcmp(cs4, c"42") != 0 { error += 1 }
    s4.free()

    // Small fractional
    s5  := String.from_float(0.1)
    cs5 := s5.cstr()
    if c_strcmp(cs5, c"0.1") != 0 { error += 1 }
    s5.free()

    error
}

// -- SSO boundary --

test_sso_boundary() {
    error := 0
    // 14 bytes should be small
    s14 := String.from_cstr(c"12345678901234")
    if s14.len() != 14zu { error += 1 }
    if !s14._is_small() { error += 1 }
    cs14 := s14.cstr()
    if c_strcmp(cs14, c"12345678901234") != 0 { error += 1 }
    // Verify cstr() didn't corrupt the string
    if s14.len() != 14zu { error += 1 }
    if !s14._is_small() { error += 1 }

    // 15 bytes should be big
    s15 := String.from_cstr(c"123456789012345")
    if s15.len() != 15zu { error += 1 }
    if s15._is_small() { error += 1 }
    cs15 := s15.cstr()
    if c_strcmp(cs15, c"123456789012345") != 0 { error += 1 }
    s14.free()
    s15.free()
    error
}

// -- Replace --

test_replace_byte() {
    error := 0
    s     := String.from_cstr(c"hello")
    r     := s.replace_byte('l', 'r')
    cs    := r.cstr()
    if c_strcmp(cs, c"herro") != 0 { error += 1 }
    r.free()

    // No matches: byte not present
    r2  := s.replace_byte('z', 'x')
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hello") != 0 { error += 1 }
    r2.free()

    s.free()

    // Empty string input
    e  := String.empty()
    re := e.replace_byte('a', 'b')
    if re.len() != 0zu { error += 1 }
    re.free()

    // All bytes matching
    s2  := String.from_cstr(c"aaa")
    r3  := s2.replace_byte('a', 'b')
    cs3 := r3.cstr()
    if c_strcmp(cs3, c"bbb") != 0 { error += 1 }
    r3.free()
    s2.free()

    error
}

// -- New function tests --

test_from_byte() {
    error := 0
    s     := String.from_byte(65u)                                              // 'A'
    if s.len() != 1zu { error += 1 }
    if (b: Some := s.byte_at(0zu) else { _NUL }, b.value) != 65u { error += 1 } // 'A'
    s.free()

    s2 := String.from_byte(122u)                                                  // 'z'
    if s2.len() != 1zu { error += 1 }
    if (b: Some := s2.byte_at(0zu) else { _NUL }, b.value) != 122u { error += 1 } // 'z'
    s2.free()
    error
}

test_eq_cstr() {
    error := 0
    s     := String.from_cstr(c"hello")
    if !s.eq_cstr(c"hello") { error += 1 }
    if s.eq_cstr(c"world") { error += 1 }
    if s.eq_cstr(c"hell") { error += 1 } // length mismatch
    s.free()

    // Both empty
    e := String.empty()
    if !e.eq_cstr(c"") { error += 1 }
    error
}

test_index_of() {
    error := 0
    s     := String.from_cstr(c"hello world")
    h     := String.from_cstr(c"hello")
    w     := String.from_cstr(c"world")
    lo    := String.from_cstr(c"lo")
    xyz   := String.from_cstr(c"xyz")
    e     := String.empty()

    if (r: Some := s.index_of(h) else { _NONE }, r.value) != 0zu { error += 1 }  // beginning
    if (r: Some := s.index_of(w) else { _NONE }, r.value) != 6zu { error += 1 }  // end
    if (r: Some := s.index_of(lo) else { _NONE }, r.value) != 3zu { error += 1 } // middle
    error += when s.index_of(xyz) { s: Some { 1 } else { 0 } }                   // not found
    if (r: Some := s.index_of(e) else { _NONE }, r.value) != 0zu { error += 1 }  // empty needle -> 0

    // Needle longer than haystack
    big := String.from_cstr(c"this is way too long")
    error += when h.index_of(big) { s: Some { 1 } else { 0 } }

    s.free()
    h.free()
    w.free()
    lo.free()
    xyz.free()
    big.free()
    error
}

test_index_of_char_from() {
    error := 0
    s     := String.from_cstr(c"hello")

    // Skip first 'l' at index 2, find second 'l' at index 3
    if (r: Some := s.index_of_char_from('l', 3zu) else { _NONE }, r.value) != 3zu { error += 1 }

    // Start at match position
    if (r: Some := s.index_of_char_from('l', 2zu) else { _NONE }, r.value) != 2zu { error += 1 }

    // Start past all occurrences
    error += when s.index_of_char_from('l', 4zu) { s: Some { 1 } else { 0 } }

    // Start >= len
    error += when s.index_of_char_from('l', 5zu) { s: Some { 1 } else { 0 } }
    error += when s.index_of_char_from('l', 100zu) { s: Some { 1 } else { 0 } }

    s.free()
    error
}

test_last_index_of_char() {
    error := 0
    s     := String.from_cstr(c"hello")

    // Multiple 'l' - finds last at index 3
    if (r: Some := s.last_index_of_char('l') else { _NONE }, r.value) != 3zu { error += 1 }

    // Single 'h' at index 0
    if (r: Some := s.last_index_of_char('h') else { _NONE }, r.value) != 0zu { error += 1 }

    // Single 'o' at end
    if (r: Some := s.last_index_of_char('o') else { _NONE }, r.value) != 4zu { error += 1 }

    // Not found
    error += when s.last_index_of_char('z') { s: Some { 1 } else { 0 } }

    // Empty string
    e := String.empty()
    error += when e.last_index_of_char('a') { s: Some { 1 } else { 0 } }

    s.free()
    error
}

test_last_index_of() {
    error := 0
    s     := String.from_cstr(c"abcabcabc")
    abc   := String.from_cstr(c"abc")
    e     := String.empty()

    // Repeated pattern - finds last
    if (r: Some := s.last_index_of(abc) else { _NONE }, r.value) != 6zu { error += 1 }

    // Empty needle -> len(s)
    if (r: Some := s.last_index_of(e) else { _NONE }, r.value) != 9zu { error += 1 }

    // Not found
    xyz := String.from_cstr(c"xyz")
    error += when s.last_index_of(xyz) { s: Some { 1 } else { 0 } }

    // Needle longer than haystack
    short := String.from_cstr(c"ab")
    long  := String.from_cstr(c"abcdef")
    error += when short.last_index_of(long) { s: Some { 1 } else { 0 } }
    short.free()
    long.free()

    // Single occurrence
    hw := String.from_cstr(c"hello world")
    w  := String.from_cstr(c"world")
    if (r: Some := hw.last_index_of(w) else { _NONE }, r.value) != 6zu { error += 1 }
    hw.free()
    w.free()

    s.free()
    abc.free()
    xyz.free()
    error
}

test_to_upper() {
    error := 0
    s     := String.from_cstr(c"hello")
    r     := s.to_upper()
    cs    := r.cstr()
    if c_strcmp(cs, c"HELLO") != 0 { error += 1 }
    r.free()

    // Mixed case
    s2  := String.from_cstr(c"Hello World")
    r2  := s2.to_upper()
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"HELLO WORLD") != 0 { error += 1 }
    r2.free()

    // Already upper
    s3  := String.from_cstr(c"ABC")
    r3  := s3.to_upper()
    cs3 := r3.cstr()
    if c_strcmp(cs3, c"ABC") != 0 { error += 1 }
    r3.free()

    // Non-alpha preserved
    s4  := String.from_cstr(c"a1b2!")
    r4  := s4.to_upper()
    cs4 := r4.cstr()
    if c_strcmp(cs4, c"A1B2!") != 0 { error += 1 }
    r4.free()

    // Empty
    e  := String.empty()
    re := e.to_upper()
    if re.len() != 0zu { error += 1 }
    re.free()

    s.free()
    s2.free()
    s3.free()
    s4.free()
    error
}

test_to_lower() {
    error := 0
    s     := String.from_cstr(c"HELLO")
    r     := s.to_lower()
    cs    := r.cstr()
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    r.free()

    // Mixed
    s2  := String.from_cstr(c"Hello World")
    r2  := s2.to_lower()
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hello world") != 0 { error += 1 }
    r2.free()

    // Already lower
    s3  := String.from_cstr(c"abc")
    r3  := s3.to_lower()
    cs3 := r3.cstr()
    if c_strcmp(cs3, c"abc") != 0 { error += 1 }
    r3.free()

    // Non-alpha preserved
    s4  := String.from_cstr(c"A1B2!")
    r4  := s4.to_lower()
    cs4 := r4.cstr()
    if c_strcmp(cs4, c"a1b2!") != 0 { error += 1 }
    r4.free()
    s4.free()

    // Empty
    e  := String.empty()
    re := e.to_lower()
    if re.len() != 0zu { error += 1 }
    re.free()

    s.free()
    s2.free()
    s3.free()
    error
}

test_trim_left() {
    error := 0

    // Leading spaces
    s  := String.from_cstr(c"  hello")
    r  := s.trim_left()
    cs := r.cstr()
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    r.free()
    s.free()

    // No leading whitespace
    s2  := String.from_cstr(c"hello  ")
    r2  := s2.trim_left()
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hello  ") != 0 { error += 1 }
    r2.free()
    s2.free()

    // All whitespace
    s3 := String.from_cstr(c"   ")
    r3 := s3.trim_left()
    if r3.len() != 0zu { error += 1 }
    r3.free()
    s3.free()

    // Empty string
    e  := String.empty()
    re := e.trim_left()
    if re.len() != 0zu { error += 1 }
    re.free()

    error
}

test_trim_right() {
    error := 0

    // Trailing spaces
    s  := String.from_cstr(c"hello  ")
    r  := s.trim_right()
    cs := r.cstr()
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    r.free()
    s.free()

    // No trailing whitespace
    s2  := String.from_cstr(c"  hello")
    r2  := s2.trim_right()
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"  hello") != 0 { error += 1 }
    r2.free()
    s2.free()

    // All whitespace
    s3 := String.from_cstr(c"   ")
    r3 := s3.trim_right()
    if r3.len() != 0zu { error += 1 }
    r3.free()
    s3.free()

    // Empty string
    e  := String.empty()
    re := e.trim_right()
    if re.len() != 0zu { error += 1 }
    re.free()

    error
}

test_trim() {
    error := 0

    // Both sides
    s  := String.from_cstr(c"  hello  ")
    r  := s.trim()
    cs := r.cstr()
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    r.free()
    s.free()

    // No whitespace
    s2  := String.from_cstr(c"hello")
    r2  := s2.trim()
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hello") != 0 { error += 1 }
    r2.free()
    s2.free()

    // All whitespace
    s3 := String.from_cstr(c"   ")
    r3 := s3.trim()
    if r3.len() != 0zu { error += 1 }
    r3.free()
    s3.free()

    // Empty
    e  := String.empty()
    re := e.trim()
    if re.len() != 0zu { error += 1 }
    re.free()

    // Tabs and newlines
    s4  := String.from_cstr(c"\t\nhello\r\n")
    r4  := s4.trim()
    cs4 := r4.cstr()
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    r4.free()
    s4.free()

    error
}

test_repeat() {
    error := 0

    // Basic repeat
    s  := String.from_cstr(c"ab")
    r  := s.repeat(3zu)
    cs := r.cstr()
    if c_strcmp(cs, c"ababab") != 0 { error += 1 }
    if r.len() != 6zu { error += 1 }
    r.free()
    s.free()

    // count=0 -> empty
    s2 := String.from_cstr(c"hello")
    r2 := s2.repeat(0zu)
    if r2.len() != 0zu { error += 1 }
    r2.free()

    // count=1 -> copy
    r3  := s2.repeat(1zu)
    cs3 := r3.cstr()
    if c_strcmp(cs3, c"hello") != 0 { error += 1 }
    r3.free()
    s2.free()

    // Empty string
    e  := String.empty()
    re := e.repeat(5zu)
    if re.len() != 0zu { error += 1 }
    re.free()

    // SSO boundary crossing: 5 bytes * 3 = 15 > 14 (SSO max)
    s4 := String.from_cstr(c"12345")
    r4 := s4.repeat(3zu)
    if r4.len() != 15zu { error += 1 }
    if r4._is_small() { error += 1 }    // should be big
    cs4 := r4.cstr()
    if c_strcmp(cs4, c"123451234512345") != 0 { error += 1 }
    r4.free()
    s4.free()

    error
}

test_replace() {
    error := 0

    // Basic replace
    s     := String.from_cstr(c"hello world")
    old   := String.from_cstr(c"world")
    new_s := String.from_cstr(c"there")
    r     := s.replace(old, new_s)
    cs    := r.cstr()
    if c_strcmp(cs, c"hello there") != 0 { error += 1 }
    r.free()
    new_s.free()
    old.free()
    s.free()

    // Multiple occurrences
    s2   := String.from_cstr(c"aaa")
    old2 := String.from_cstr(c"a")
    new2 := String.from_cstr(c"bb")
    r2   := s2.replace(old2, new2)
    cs2  := r2.cstr()
    if c_strcmp(cs2, c"bbbbbb") != 0 { error += 1 }
    r2.free()
    new2.free()
    old2.free()
    s2.free()

    // No match -> copy
    s3   := String.from_cstr(c"hello")
    old3 := String.from_cstr(c"xyz")
    new3 := String.from_cstr(c"abc")
    r3   := s3.replace(old3, new3)
    cs3  := r3.cstr()
    if c_strcmp(cs3, c"hello") != 0 { error += 1 }
    r3.free()
    new3.free()
    old3.free()
    s3.free()

    // Replace with empty (deletion)
    s4   := String.from_cstr(c"hello world")
    old4 := String.from_cstr(c" world")
    new4 := String.empty()
    r4   := s4.replace(old4, new4)
    cs4  := r4.cstr()
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    r4.free()
    old4.free()
    s4.free()

    // Replace with longer string
    s5   := String.from_cstr(c"a-b-c")
    old5 := String.from_cstr(c"-")
    new5 := String.from_cstr(c"--")
    r5   := s5.replace(old5, new5)
    cs5  := r5.cstr()
    if c_strcmp(cs5, c"a--b--c") != 0 { error += 1 }
    r5.free()
    new5.free()
    old5.free()
    s5.free()

    // Replace crossing SSO boundary: c"ab-cd-ef" (8) -> c"ab___cd___ef" (12+) wait...
    // c"ab-cd-ef" replacing c"-" with c"-----" = c"ab-----cd-----ef" = 16 bytes
    s6   := String.from_cstr(c"ab-cd-ef")
    old6 := String.from_cstr(c"-")
    new6 := String.from_cstr(c"-----")
    r6   := s6.replace(old6, new6)
    if r6.len() != 16zu { error += 1 }
    if r6._is_small() { error += 1 }
    cs6 := r6.cstr()
    if c_strcmp(cs6, c"ab-----cd-----ef") != 0 { error += 1 }
    r6.free()
    new6.free()
    old6.free()
    s6.free()

    // Replace on empty string (old longer than s -> copy)
    s7   := String.empty()
    old7 := String.from_cstr(c"x")
    new7 := String.from_cstr(c"y")
    r7   := s7.replace(old7, new7)
    if r7.len() != 0zu { error += 1 }
    r7.free()
    new7.free()
    old7.free()

    error
}

test_split() {
    error := 0

    // Basic split
    s   := String.from_cstr(c"a,b,c")
    d   := String.from_cstr(c",")
    arr := s.split(d)
    if arr.size != 3zu { error += 1 }
    e0 := arr.get(0zu)
    e1 := arr.get(1zu)
    e2 := arr.get(2zu)
    if !e0.eq_cstr(c"a") { error += 1 }
    if !e1.eq_cstr(c"b") { error += 1 }
    if !e2.eq_cstr(c"c") { error += 1 }
    e0.free() e1.free() e2.free()
    arr.free()
    d.free()
    s.free()

    // Consecutive delimiters -> empty strings
    s2   := String.from_cstr(c"a,,b")
    d2   := String.from_cstr(c",")
    arr2 := s2.split(d2)
    if arr2.size != 3zu { error += 1 }
    f0 := arr2.get(0zu)
    f1 := arr2.get(1zu)
    f2 := arr2.get(2zu)
    if !f0.eq_cstr(c"a") { error += 1 }
    if !f1.is_empty() { error += 1 }
    if !f2.eq_cstr(c"b") { error += 1 }
    f0.free() f1.free() f2.free()
    arr2.free()
    d2.free()
    s2.free()

    // Trailing delimiter
    s3   := String.from_cstr(c"a,b,")
    d3   := String.from_cstr(c",")
    arr3 := s3.split(d3)
    if arr3.size != 3zu { error += 1 }
    g0 := arr3.get(0zu)
    g1 := arr3.get(1zu)
    g2 := arr3.get(2zu)
    if !g0.eq_cstr(c"a") { error += 1 }
    if !g1.eq_cstr(c"b") { error += 1 }
    if !g2.is_empty() { error += 1 }
    g0.free() g1.free() g2.free()
    arr3.free()
    d3.free()
    s3.free()

    // Empty string -> [c""]
    e    := String.empty()
    d4   := String.from_cstr(c",")
    arr4 := e.split(d4)
    if arr4.size != 1zu { error += 1 }
    h0 := arr4.get(0zu)
    if !h0.is_empty() { error += 1 }
    h0.free()
    arr4.free()
    d4.free()

    // Multi-char delimiter
    s5   := String.from_cstr(c"one::two::three")
    d5   := String.from_cstr(c"::")
    arr5 := s5.split(d5)
    if arr5.size != 3zu { error += 1 }
    i0 := arr5.get(0zu)
    i1 := arr5.get(1zu)
    i2 := arr5.get(2zu)
    if !i0.eq_cstr(c"one") { error += 1 }
    if !i1.eq_cstr(c"two") { error += 1 }
    if !i2.eq_cstr(c"three") { error += 1 }
    i0.free() i1.free() i2.free()
    arr5.free()
    d5.free()
    s5.free()

    // Delimiter not found -> single element
    s6   := String.from_cstr(c"hello")
    d6   := String.from_cstr(c",")
    arr6 := s6.split(d6)
    if arr6.size != 1zu { error += 1 }
    j0 := arr6.get(0zu)
    if !j0.eq_cstr(c"hello") { error += 1 }
    j0.free()
    arr6.free()
    d6.free()
    s6.free()

    // Leading delimiter
    s7   := String.from_cstr(c",a,b")
    d7   := String.from_cstr(c",")
    arr7 := s7.split(d7)
    if arr7.size != 3zu { error += 1 }
    k0 := arr7.get(0zu)
    k1 := arr7.get(1zu)
    k2 := arr7.get(2zu)
    if !k0.is_empty() { error += 1 }
    if !k1.eq_cstr(c"a") { error += 1 }
    if !k2.eq_cstr(c"b") { error += 1 }
    k0.free() k1.free() k2.free()
    arr7.free()
    d7.free()
    s7.free()

    error
}

test_join() {
    error := 0

    // Basic join
    arr := Array.empty[String.String]()
    a   := String.from_cstr(c"a")
    b   := String.from_cstr(c"b")
    c   := String.from_cstr(c"c")
    arr.push(a)
    arr.push(b)
    arr.push(c)
    sep := String.from_cstr(c",")
    r   := sep.join(arr)
    cs  := r.cstr()
    if c_strcmp(cs, c"a,b,c") != 0 { error += 1 }
    r.free()
    sep.free()
    a.free() b.free() c.free()
    arr.free()

    // Empty separator
    arr2 := Array.empty[String.String]()
    a2   := String.from_cstr(c"x")
    b2   := String.from_cstr(c"y")
    arr2.push(a2)
    arr2.push(b2)
    sep2 := String.empty()
    r2   := sep2.join(arr2)
    cs2  := r2.cstr()
    if c_strcmp(cs2, c"xy") != 0 { error += 1 }
    r2.free()
    a2.free() b2.free()
    arr2.free()

    // Single element
    arr3 := Array.empty[String.String]()
    a3   := String.from_cstr(c"only")
    arr3.push(a3)
    sep3 := String.from_cstr(c",")
    r3   := sep3.join(arr3)
    cs3  := r3.cstr()
    if c_strcmp(cs3, c"only") != 0 { error += 1 }
    r3.free()
    sep3.free()
    a3.free()
    arr3.free()

    // Empty array
    arr4 := Array.empty[String.String]()
    sep4 := String.from_cstr(c",")
    r4   := sep4.join(arr4)
    if r4.len() != 0zu { error += 1 }
    r4.free()
    sep4.free()
    arr4.free()

    // Empty elements -> separators only
    arr5 := Array.empty[String.String]()
    e1   := String.empty()
    e2   := String.empty()
    e3   := String.empty()
    arr5.push(e1)
    arr5.push(e2)
    arr5.push(e3)
    sep5 := String.from_cstr(c",")
    r5   := sep5.join(arr5)
    cs5  := r5.cstr()
    if c_strcmp(cs5, c",,") != 0 { error += 1 }
    r5.free()
    sep5.free()
    arr5.free()

    // Multi-char separator
    arr6 := Array.empty[String.String]()
    a6   := String.from_cstr(c"a")
    b6   := String.from_cstr(c"b")
    c6   := String.from_cstr(c"c")
    arr6.push(a6)
    arr6.push(b6)
    arr6.push(c6)
    sep6 := String.from_cstr(c" :: ")
    r6   := sep6.join(arr6)
    cs6  := r6.cstr()
    if c_strcmp(cs6, c"a :: b :: c") != 0 { error += 1 }
    r6.free()
    sep6.free()
    a6.free() b6.free() c6.free()
    arr6.free()

    // Join crossing SSO boundary (result > 14 bytes)
    arr7 := Array.empty[String.String]()
    a7   := String.from_cstr(c"hello")
    b7   := String.from_cstr(c"world")
    arr7.push(a7)
    arr7.push(b7)
    sep7 := String.from_cstr(c"-----")
    r7   := sep7.join(arr7)             // c"hello-----world" = 15 bytes
    if r7.len() != 15zu { error += 1 }
    if r7._is_small() { error += 1 }
    cs7 := r7.cstr()
    if c_strcmp(cs7, c"hello-----world") != 0 { error += 1 }
    r7.free()
    sep7.free()
    a7.free() b7.free()
    arr7.free()

    error
}

test_iter() {
    error := 0

    // Sum bytes via for loop
    s          := String.from_cstr(c"abc")
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
    s2 := String.from_cstr(c"Hello!")
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
    s2.free()

    s.free()

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
    s     := String.from_cstr(c"hello")

    // Multiple occurrences: 'l' appears twice
    if s.count_byte('l') != 2zu { error += 1 }

    // Single occurrence
    if s.count_byte('h') != 1zu { error += 1 }

    // Zero occurrences
    if s.count_byte('z') != 0zu { error += 1 }

    s.free()

    // Empty string
    e := String.empty()
    if e.count_byte('a') != 0zu { error += 1 }

    error
}

test_count() {
    error := 0

    // Non-overlapping matches: c"ab" in c"abababab" -> 4
    s  := String.from_cstr(c"abababab")
    ab := String.from_cstr(c"ab")
    if s.count(ab) != 4zu { error += 1 }
    ab.free()

    // Non-overlapping: c"abab" in c"abababab" -> 2
    abab := String.from_cstr(c"abab")
    if s.count(abab) != 2zu { error += 1 }
    abab.free()
    s.free()

    // Single match
    s2 := String.from_cstr(c"hello world")
    w  := String.from_cstr(c"world")
    if s2.count(w) != 1zu { error += 1 }
    w.free()

    // No match
    xyz := String.from_cstr(c"xyz")
    if s2.count(xyz) != 0zu { error += 1 }
    xyz.free()
    s2.free()

    // Needle longer than haystack
    short  := String.from_cstr(c"ab")
    long_n := String.from_cstr(c"abcdef")
    if short.count(long_n) != 0zu { error += 1 }
    short.free()
    long_n.free()

    // Needle == haystack
    s3 := String.from_cstr(c"hello")
    if s3.count(s3) != 1zu { error += 1 }
    s3.free()

    // Single-char needle matching all bytes
    s4 := String.from_cstr(c"aaa")
    a2 := String.from_cstr(c"a")
    if s4.count(a2) != 3zu { error += 1 }
    s4.free()
    a2.free()

    error
}

test_strip_prefix() {
    error := 0

    // Prefix present
    s  := String.from_cstr(c"hello world")
    p  := String.from_cstr(c"hello ")
    r  := s.strip_prefix(p)
    cs := r.cstr()
    if c_strcmp(cs, c"world") != 0 { error += 1 }
    r.free()
    p.free()

    // Prefix absent -> copy
    p2  := String.from_cstr(c"xyz")
    r2  := s.strip_prefix(p2)
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hello world") != 0 { error += 1 }
    r2.free()
    p2.free()

    // Prefix == entire string -> empty
    r3 := s.strip_prefix(s)
    if r3.len() != 0zu { error += 1 }
    r3.free()

    s.free()

    // Empty prefix -> copy
    s2  := String.from_cstr(c"hello")
    e   := String.empty()
    r4  := s2.strip_prefix(e)
    cs4 := r4.cstr()
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    r4.free()
    s2.free()

    error
}

test_strip_suffix() {
    error := 0

    // Suffix present
    s  := String.from_cstr(c"hello world")
    x  := String.from_cstr(c" world")
    r  := s.strip_suffix(x)
    cs := r.cstr()
    if c_strcmp(cs, c"hello") != 0 { error += 1 }
    r.free()
    x.free()

    // Suffix absent -> copy
    x2  := String.from_cstr(c"xyz")
    r2  := s.strip_suffix(x2)
    cs2 := r2.cstr()
    if c_strcmp(cs2, c"hello world") != 0 { error += 1 }
    r2.free()
    x2.free()

    // Suffix == entire string -> empty
    r3 := s.strip_suffix(s)
    if r3.len() != 0zu { error += 1 }
    r3.free()

    s.free()

    // Empty suffix -> copy
    s2  := String.from_cstr(c"hello")
    e   := String.empty()
    r4  := s2.strip_suffix(e)
    cs4 := r4.cstr()
    if c_strcmp(cs4, c"hello") != 0 { error += 1 }
    r4.free()
    s2.free()

    error
}

test_from_bool() {
    error := 0
    t     := String.from_bool(true)
    cs    := t.cstr()
    if c_strcmp(cs, c"true") != 0 { error += 1 }
    if t.len() != 4zu { error += 1 }
    if !t._is_small() { error += 1 }
    t.free()

    f   := String.from_bool(false)
    cs2 := f.cstr()
    if c_strcmp(cs2, c"false") != 0 { error += 1 }
    if f.len() != 5zu { error += 1 }
    if !f._is_small() { error += 1 }
    f.free()

    error
}

test_from_array() {
    error := 0

    // Array[CChar]
    a := Array.empty[CChar]()
    a.push('h')
    a.push('e')
    a.push('l')
    a.push('l')
    a.push('o')
    s := String.from_array(a)
    if s.len() != 5 { error += 1 }
    if !s.eq_cstr(c"hello") { error += 1 }
    s.free()
    a.free()

    // Array[Byte]
    b        := Array.empty[Byte]()
    b1: Byte := 0x41
    b2: Byte := 0x42
    b3: Byte := 0x43
    b.push(b1)
    b.push(b2)
    b.push(b3)
    s2 := String.from_array(b)
    if s2.len() != 3 { error += 1 }
    if !s2.eq_cstr(c"ABC") { error += 1 }
    s2.free()
    b.free()

    error
}

test_parse_number() {
    error := 0

    error += when "42".parse_number() {
        opt: Some {
            when opt.value {
                n: Integer { if n.value == 42 { 0 } else { 1 } }
                else { 1 }
            }
        }
        else { 1 }
    }

    error += when "-5".parse_number() {
        opt: Some {
            when opt.value {
                n: Integer { if n.value == -5 { 0 } else { 1 } }
                else { 1 }
            }
        }
        else { 1 }
    }

    error += when "9999999999999999999".parse_number() {
        opt: Some {
            when opt.value {
                u: Unsigned { 0 } else { 1 }
            }
        }
        else { 1 }
    }

    error += when "3.14".parse_number() {
        opt: Some {
            when opt.value { f: Floating { 0 }
                else { 1 }
            }
        }
        else { 1 }
    }


    if (c_LDBL_MAX_10_EXP > c_DBL_MAX_10_EXP) {
        // Don't bother testing if the two types have the same max range
        c_assert(c_LDBL_MAX_10_EXP >= 400)
        p1: Some := "1e400".parse_number() else { return error + 1 }
        p2: Some := "2e400".parse_number() else { return error + 1 }

        ld1: LDouble := p1.value else { return error + 1 }
        ld2: LDouble := p2.value else { return error + 1 }

        // ensure two distinct values (they would both be `inf` if regular double)
        error += if ld1.value != ld2.value {0} else {1}
    }

    error += when "hello".parse_number() {
        opt: Some { 1 } else { 0 }
    }

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
    error += _check(c"eq_operator", test_eq_operator())
    error += _check(c"cmp", test_cmp())
    error += _check(c"starts_with", test_starts_with())
    error += _check(c"ends_with", test_ends_with())
    error += _check(c"contains", test_contains())
    error += _check(c"contains_char", test_contains_char())
    error += _check(c"cat_small_small", test_cat_small_small())
    error += _check(c"cat_to_big", test_cat_to_big())
    error += _check(c"push", test_push())
    error += _check(c"cat_3", test_cat_3())
    error += _check(c"byte_at", test_byte_at())
    error += _check(c"char_at", test_char_at())
    error += _check(c"copy_range", test_copy_range())
    error += _check(c"slice", test_slice())
    error += _check(c"index_of_char", test_index_of_char())
    error += _check(c"from_int", test_from_int())
    error += _check(c"from_float", test_from_float())
    error += _check(c"sso_boundary", test_sso_boundary())
    error += _check(c"replace_byte", test_replace_byte())
    error += _check(c"from_byte", test_from_byte())
    error += _check(c"eq_cstr", test_eq_cstr())
    error += _check(c"index_of", test_index_of())
    error += _check(c"index_of_char_from", test_index_of_char_from())
    error += _check(c"last_index_of_char", test_last_index_of_char())
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
    error += _check(c"from_array", test_from_array())
    error += _check(c"parse_number", test_parse_number())
    result: CInt := error
    result
}
