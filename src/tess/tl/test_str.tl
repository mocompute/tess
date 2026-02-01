#module main
#import <Str.tl>
#import <stdio.tl>
#import <string.tl>

// -- Construction and basic queries --

test_empty() {
    error := 0
    s     := Str.empty()
    if Str.len(s) != 0 { error += 1 }
    if !Str.is_empty(s) { error += 1 }
    error
}

test_from_cstr_small() {
    error := 0
    s     := "hello"
    if Str.len(s) != 5 { error += 1 }
    if Str.is_empty(s) { error += 1 }
    error
}

test_from_cstr_big() {
    error := 0
    s     := "this is a longer string!!"
    if Str.len(s) != 25 { error += 1 }
    if Str.is_empty(s) { error += 1 }
    error
}

test_from_bytes() {
    error      := 0
    p: CString := c"hello"
    s          := Str.from_bytes(p, 5)
    if Str.len(s) != 5 { error += 1 }
    Str.free(s.&)

    // With embedded null
    p2: CString := c"ab\0cd"
    s2          := Str.from_bytes(p2, 5)
    if Str.len(s2) != 5 { error += 1 }
    Str.free(s2.&)
    error
}

test_copy_small() {
    error := 0
    s     := "hello"
    c     := Str.copy(s)
    if Str.len(c) != 5 { error += 1 }
    // Should be independent
    s = Str.empty()                     // cannot free Str literal
    if Str.len(c) != 5 { error += 1 }
    Str.free(c.&)
    error
}

test_copy_big() {
    error := 0
    s     := "this is a longer string!!"
    c     := Str.copy(s)
    if Str.len(c) != 25 { error += 1 }
    s = Str.empty()                     // cannot free Str literal
    if Str.len(c) != 25 { error += 1 }
    Str.free(c.&)
    error
}

// -- C string interop --

test_cstr() {
    error := 0
    s     := "hello"
    cs    := Str.cstr(s.&)
    if c_strcmp(cs, c"hello") != 0 { error += 1 }

    s2  := "this is a longer string!!"
    cs2 := Str.cstr(s2.&)
    if c_strcmp(cs2, c"this is a longer string!!") != 0 { error += 1 }
    error
}

// -- Comparison --

test_eq() {
    error := 0
    a     := "hello"
    b     := "hello"
    c     := "world"
    if !Str.eq(a, b) { error += 1 }
    if Str.eq(a, c) { error += 1 }

    // Empty strings
    e1 := Str.empty()
    e2 := Str.empty()
    if !Str.eq(e1, e2) { error += 1 }

    error
}

test_cmp() {
    error := 0
    a     := "abc"
    b     := "abd"
    c     := "abc"
    if Str.cmp(a, b) >= 0 { error += 1 }
    if Str.cmp(b, a) <= 0 { error += 1 }
    if Str.cmp(a, c) != 0 { error += 1 }
    error
}

test_starts_with() {
    error := 0
    s     := "hello world"
    a     := "hello"
    b     := "world"
    e     := Str.empty()
    if !Str.starts_with(s, a) { error += 1 }
    if Str.starts_with(s, b) { error += 1 }
    if !Str.starts_with(s, e) { error += 1 }
    error
}

test_ends_with() {
    error := 0
    s     := "hello world"
    a     := "world"
    b     := "hello"
    e     := Str.empty()
    if !Str.ends_with(s, a) { error += 1 }
    if Str.ends_with(s, b) { error += 1 }
    if !Str.ends_with(s, e) { error += 1 }
    error
}

test_contains() {
    error := 0
    s     := "hello world"
    a     := "lo wo"
    b     := "xyz"
    e     := Str.empty()
    if !Str.contains(s, a) { error += 1 }
    if Str.contains(s, b) { error += 1 }
    if !Str.contains(s, e) { error += 1 }
    error
}

test_contains_byte() {
    error := 0
    s     := "hello"
    if !Str.contains_byte(s, 104) { error += 1 } // 'h'
    if Str.contains_byte(s, 122) { error += 1 }  // 'z'
    error
}

// -- Concatenation --

test_cat_small_small() {
    error := 0
    a     := "hi"
    b     := " there"
    c     := Str.cat(a, b)
    if Str.len(c) != 8 { error += 1 }
    cs := Str.cstr(c.&)
    if c_strcmp(cs, c"hi there") != 0 { error += 1 }
    Str.free(c.&)
    error
}

test_cat_to_big() {
    error := 0
    a     := "hello "
    b     := "beautiful world!"
    c     := Str.cat(a, b)
    if Str.len(c) != 22 { error += 1 }
    cs := Str.cstr(c.&)
    if c_strcmp(cs, c"hello beautiful world!") != 0 { error += 1 }
    Str.free(c.&)
    error
}

test_push() {
    error := 0
    s     := "hello"
    b     := " world"
    Str.push(s.&, b)
    if Str.len(s) != 11 { error += 1 }
    cs := Str.cstr(s.&)
    if c_strcmp(cs, c"hello world") != 0 { error += 1 }
    Str.free(s.&)
    error
}

test_cat_3() {
    error := 0
    a     := "one"
    b     := " two"
    c     := " three"
    r     := Str.cat_3(a, b, c)
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"one two three") != 0 { error += 1 }
    Str.free(r.&)
    error
}

// -- Access and slicing --

test_byte_at() {
    error := 0
    s     := "abc"
    if Str.byte_at(s, 0) != 97 { error += 1 } // 'a'
    if Str.byte_at(s, 1) != 98 { error += 1 } // 'b'
    if Str.byte_at(s, 2) != 99 { error += 1 } // 'c'
    error
}

test_slice() {
    error := 0
    s     := "hello world"
    sub   := Str.slice(s, 6, 11)
    if Str.len(sub) != 5 { error += 1 }
    cs := Str.cstr(sub.&)
    if c_strcmp(cs, c"world") != 0 { error += 1 }

    // Empty slice
    e := Str.slice(s, 3, 3)
    if Str.len(e) != 0 { error += 1 }

    Str.free(s.&)
    Str.free(sub.&)
    Str.free(e.&)
    error
}

// -- Search --

test_index_of_byte() {
    error := 0
    s     := "hello"
    if Str.index_of_byte(s, 108) != 2 { error += 1 }  // 'l'
    if Str.index_of_byte(s, 111) != 4 { error += 1 }  // 'o'
    if Str.index_of_byte(s, 122) != -1 { error += 1 } // 'z' not found
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

// -- SSO boundary --

test_sso_boundary() {
    error := 0
    // 14 bytes should be small
    s14 := "12345678901234"
    if Str.len(s14) != 14 { error += 1 }
    if !Str._is_small(s14) { error += 1 }
    cs14 := Str.cstr(s14.&)
    if c_strcmp(cs14, c"12345678901234") != 0 { error += 1 }
    // Verify cstr() didn't corrupt the string
    if Str.len(s14) != 14 { error += 1 }
    if !Str._is_small(s14) { error += 1 }

    // 15 bytes should be big
    s15 := "123456789012345"
    if Str.len(s15) != 15 { error += 1 }
    if Str._is_small(s15) { error += 1 }
    cs15 := Str.cstr(s15.&)
    if c_strcmp(cs15, c"123456789012345") != 0 { error += 1 }
    error
}

// -- Replace --

test_replace_byte() {
    error := 0
    s     := "hello"
    r     := Str.replace_byte(s, 108, 114) // 'l' -> 'r'
    cs    := Str.cstr(r.&)
    if c_strcmp(cs, c"herro") != 0 { error += 1 }
    Str.free(r.&)
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
    error += _check(c"byte_at", test_byte_at())
    error += _check(c"slice", test_slice())
    error += _check(c"index_of_byte", test_index_of_byte())
    error += _check(c"from_int", test_from_int())
    error += _check(c"sso_boundary", test_sso_boundary())
    error += _check(c"replace_byte", test_replace_byte())
    error
}
