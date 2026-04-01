#import <Hash.tl>

#module main
#import <HashMap.tl>
#import <String.tl>
#import <Array.tl>

test_str_key_basic() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr(c"hello"), 10)
    m.set(String.from_cstr(c"world"), 20)
    m.set(String.from_cstr(c"foo"), 30)

    if m.size() != 3zu { error += 1 }

    p := when m.get(String.from_cstr(c"hello")) {
        s: Some { s.value }
        n: None { null }
    }
    if p == null { error += 1 }
    if p != null && p.* != 10 { error += 1 }

    p = when m.get(String.from_cstr(c"world")) {
        s: Some { s.value }
        n: None { null }
    }
    if p == null { error += 1 }
    if p != null && p.* != 20 { error += 1 }

    p = when m.get(String.from_cstr(c"foo")) {
        s: Some { s.value }
        n: None { null }
    }
    if p == null { error += 1 }
    if p != null && p.* != 30 { error += 1 }

    // Missing key
    if m.get(String.from_cstr(c"bar")).or_else(null) != null { error += 1 }

    m.destroy()
    error
}

test_str_key_overwrite() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr(c"key"), 100)
    m.set(String.from_cstr(c"key"), 200)

    if m.size() != 1zu { error += 1 }

    p := m.get(String.from_cstr(c"key")).or_else(null)
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    m.destroy()
    error
}

test_str_key_remove() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr(c"a"), 1)
    m.set(String.from_cstr(c"b"), 2)
    m.set(String.from_cstr(c"c"), 3)

    if !m.remove(String.from_cstr(c"b")) { error += 1 }
    if m.size() != 2zu { error += 1 }
    if m.contains(String.from_cstr(c"b")) { error += 1 }
    if !m.contains(String.from_cstr(c"a")) { error += 1 }
    if !m.contains(String.from_cstr(c"c")) { error += 1 }

    m.destroy()
    error
}

test_str_key_empty_string() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr(c""), 42)
    if m.size() != 1zu { error += 1 }

    p := m.get(String.from_cstr(c"")).or_else(null)
    if p == null { error += 1 }
    if p != null && p.* != 42 { error += 1 }

    // Empty string and non-empty must be different keys
    m.set(String.from_cstr(c"x"), 99)
    if m.size() != 2zu { error += 1 }

    m.destroy()
    error
}

test_str_key_long_strings() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    // These exceed SSO threshold (14 bytes), exercising big-string hashing
    m.set(String.from_cstr(c"this is a long string key"), 1)
    m.set(String.from_cstr(c"another lengthy key value"), 2)

    if m.size() != 2zu { error += 1 }

    p := m.get(String.from_cstr(c"this is a long string key")).or_else(null)
    if p == null { error += 1 }
    if p != null && p.* != 1 { error += 1 }

    p = m.get(String.from_cstr(c"another lengthy key value")).or_else(null)
    if p == null { error += 1 }
    if p != null && p.* != 2 { error += 1 }

    m.destroy()
    error
}

main() -> CInt {
    error: CInt := 0
    error += test_str_key_basic()
    error += test_str_key_overwrite()
    error += test_str_key_remove()
    error += test_str_key_empty_string()
    error += test_str_key_long_strings()
    error
}
