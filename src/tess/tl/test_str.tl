#module main
#import <Str.tl>
#import <stdio.tl>
#import <string.tl>

// -- Construction and basic queries --

test_empty() {
    error := 0
    s := String.empty()
    if String.len(s) != 0 { error += 1 }
    if !String.is_empty(s) { error += 1 }
    error
}

test_from_cstr_small() {
    error := 0
    s := String.from_cstr("hello")
    if String.len(s) != 5 { error += 1 }
    if String.is_empty(s) { error += 1 }
    String.free(s.&)
    error
}

test_from_cstr_big() {
    error := 0
    s := String.from_cstr("this is a longer string!!")
    if String.len(s) != 25 { error += 1 }
    if String.is_empty(s) { error += 1 }
    String.free(s.&)
    error
}

test_from_bytes() {
    error := 0
    p: CString := "hello"
    s := String.from_bytes(p, 5)
    if String.len(s) != 5 { error += 1 }
    String.free(s.&)

    // With embedded null
    p2: CString := "ab\0cd"
    s2 := String.from_bytes(p2, 5)
    if String.len(s2) != 5 { error += 1 }
    String.free(s2.&)
    error
}

test_copy_small() {
    error := 0
    s := String.from_cstr("hello")
    c := String.copy(s)
    if String.len(c) != 5 { error += 1 }
    // Should be independent
    String.free(s.&)
    if String.len(c) != 5 { error += 1 }
    String.free(c.&)
    error
}

test_copy_big() {
    error := 0
    s := String.from_cstr("this is a longer string!!")
    c := String.copy(s)
    if String.len(c) != 25 { error += 1 }
    String.free(s.&)
    if String.len(c) != 25 { error += 1 }
    String.free(c.&)
    error
}

// -- C string interop --

test_cstr() {
    error := 0
    s := String.from_cstr("hello")
    cs := String.cstr(s.&)
    if c_strcmp(cs, "hello") != 0 { error += 1 }
    String.free(s.&)

    s2 := String.from_cstr("this is a longer string!!")
    cs2 := String.cstr(s2.&)
    if c_strcmp(cs2, "this is a longer string!!") != 0 { error += 1 }
    String.free(s2.&)
    error
}

// -- Comparison --

test_eq() {
    error := 0
    a := String.from_cstr("hello")
    b := String.from_cstr("hello")
    c := String.from_cstr("world")
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
    a := String.from_cstr("abc")
    b := String.from_cstr("abd")
    c := String.from_cstr("abc")
    if String.cmp(a, b) >= 0 { error += 1 }
    if String.cmp(b, a) <= 0 { error += 1 }
    if String.cmp(a, c) != 0 { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    error
}

test_starts_with() {
    error := 0
    s := String.from_cstr("hello world")
    a := String.from_cstr("hello")
    b := String.from_cstr("world")
    e := String.empty()
    if !String.starts_with(s, a) { error += 1 }
    if String.starts_with(s, b) { error += 1 }
    if !String.starts_with(s, e) { error += 1 }
    String.free(s.&)
    String.free(a.&)
    String.free(b.&)
    error
}

test_ends_with() {
    error := 0
    s := String.from_cstr("hello world")
    a := String.from_cstr("world")
    b := String.from_cstr("hello")
    e := String.empty()
    if !String.ends_with(s, a) { error += 1 }
    if String.ends_with(s, b) { error += 1 }
    if !String.ends_with(s, e) { error += 1 }
    String.free(s.&)
    String.free(a.&)
    String.free(b.&)
    error
}

test_contains() {
    error := 0
    s := String.from_cstr("hello world")
    a := String.from_cstr("lo wo")
    b := String.from_cstr("xyz")
    e := String.empty()
    if !String.contains(s, a) { error += 1 }
    if String.contains(s, b) { error += 1 }
    if !String.contains(s, e) { error += 1 }
    String.free(s.&)
    String.free(a.&)
    String.free(b.&)
    error
}

test_contains_byte() {
    error := 0
    s := String.from_cstr("hello")
    if !String.contains_byte(s, 104) { error += 1 }  // 'h'
    if String.contains_byte(s, 122)  { error += 1 }  // 'z'
    String.free(s.&)
    error
}

// -- Concatenation --

test_cat_small_small() {
    error := 0
    a := String.from_cstr("hi")
    b := String.from_cstr(" there")
    c := String.cat(a, b)
    if String.len(c) != 8 { error += 1 }
    cs := String.cstr(c.&)
    if c_strcmp(cs, "hi there") != 0 { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    error
}

test_cat_to_big() {
    error := 0
    a := String.from_cstr("hello ")
    b := String.from_cstr("beautiful world!")
    c := String.cat(a, b)
    if String.len(c) != 22 { error += 1 }
    cs := String.cstr(c.&)
    if c_strcmp(cs, "hello beautiful world!") != 0 { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    error
}

test_push() {
    error := 0
    s := String.from_cstr("hello")
    b := String.from_cstr(" world")
    String.push(s.&, b)
    if String.len(s) != 11 { error += 1 }
    cs := String.cstr(s.&)
    if c_strcmp(cs, "hello world") != 0 { error += 1 }
    String.free(s.&)
    String.free(b.&)
    error
}

test_cat_3() {
    error := 0
    a := String.from_cstr("one")
    b := String.from_cstr(" two")
    c := String.from_cstr(" three")
    r := String.cat_3(a, b, c)
    cs := String.cstr(r.&)
    if c_strcmp(cs, "one two three") != 0 { error += 1 }
    String.free(a.&)
    String.free(b.&)
    String.free(c.&)
    String.free(r.&)
    error
}

// -- Access and slicing --

test_byte_at() {
    error := 0
    s := String.from_cstr("abc")
    if String.byte_at(s, 0) != 97 { error += 1 }  // 'a'
    if String.byte_at(s, 1) != 98 { error += 1 }  // 'b'
    if String.byte_at(s, 2) != 99 { error += 1 }  // 'c'
    String.free(s.&)
    error
}

test_slice() {
    error := 0
    s := String.from_cstr("hello world")
    sub := String.slice(s, 6, 11)
    if String.len(sub) != 5 { error += 1 }
    cs := String.cstr(sub.&)
    if c_strcmp(cs, "world") != 0 { error += 1 }

    // Empty slice
    e := String.slice(s, 3, 3)
    if String.len(e) != 0 { error += 1 }

    String.free(s.&)
    String.free(sub.&)
    String.free(e.&)
    error
}

// -- Search --

test_index_of_byte() {
    error := 0
    s := String.from_cstr("hello")
    if String.index_of_byte(s, 108) != 2 { error += 1 }  // 'l'
    if String.index_of_byte(s, 111) != 4 { error += 1 }  // 'o'
    if String.index_of_byte(s, 122) != -1 { error += 1 }  // 'z' not found
    String.free(s.&)
    error
}

// -- Conversion --

test_from_int() {
    error := 0
    s := String.from_int(42)
    cs := String.cstr(s.&)
    if c_strcmp(cs, "42") != 0 { error += 1 }
    String.free(s.&)

    s2 := String.from_int(-123)
    cs2 := String.cstr(s2.&)
    if c_strcmp(cs2, "-123") != 0 { error += 1 }
    String.free(s2.&)

    s3 := String.from_int(0)
    cs3 := String.cstr(s3.&)
    if c_strcmp(cs3, "0") != 0 { error += 1 }
    String.free(s3.&)
    error
}

// -- SSO boundary --

test_sso_boundary() {
    error := 0
    // 14 bytes should be small
    s14 := String.from_cstr("12345678901234")
    if String.len(s14) != 14 { error += 1 }
    if !String._is_small(s14) { error += 1 }
    cs14 := String.cstr(s14.&)
    if c_strcmp(cs14, "12345678901234") != 0 { error += 1 }
    // Verify cstr() didn't corrupt the string
    if String.len(s14) != 14 { error += 1 }
    if !String._is_small(s14) { error += 1 }
    String.free(s14.&)

    // 15 bytes should be big
    s15 := String.from_cstr("123456789012345")
    if String.len(s15) != 15 { error += 1 }
    if String._is_small(s15) { error += 1 }
    cs15 := String.cstr(s15.&)
    if c_strcmp(cs15, "123456789012345") != 0 { error += 1 }
    String.free(s15.&)
    error
}

// -- Replace --

test_replace_byte() {
    error := 0
    s := String.from_cstr("hello")
    r := String.replace_byte(s, 108, 114)  // 'l' -> 'r'
    cs := String.cstr(r.&)
    if c_strcmp(cs, "herro") != 0 { error += 1 }
    String.free(s.&)
    String.free(r.&)
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
    error += _check("cstr", test_cstr())
    error += _check("eq", test_eq())
    error += _check("cmp", test_cmp())
    error += _check("starts_with", test_starts_with())
    error += _check("ends_with", test_ends_with())
    error += _check("contains", test_contains())
    error += _check("contains_byte", test_contains_byte())
    error += _check("cat_small_small", test_cat_small_small())
    error += _check("cat_to_big", test_cat_to_big())
    error += _check("push", test_push())
    error += _check("cat_3", test_cat_3())
    error += _check("byte_at", test_byte_at())
    error += _check("slice", test_slice())
    error += _check("index_of_byte", test_index_of_byte())
    error += _check("from_int", test_from_int())
    error += _check("sso_boundary", test_sso_boundary())
    error += _check("replace_byte", test_replace_byte())
    error
}
