#import <Hash.tl>

#module main
#import <HashMap.tl>
#import <String.tl>
#import <Array.tl>

test_str_key_basic() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr("hello"), 10)
    m.set(String.from_cstr("world"), 20)
    m.set(String.from_cstr("foo"), 30)

    if m.size() != 3zu { error += 1 }

    p := m.get(String.from_cstr("hello"))
    if p == null { error += 1 }
    if p != null && p.* != 10 { error += 1 }

    p = m.get(String.from_cstr("world"))
    if p == null { error += 1 }
    if p != null && p.* != 20 { error += 1 }

    p = m.get(String.from_cstr("foo"))
    if p == null { error += 1 }
    if p != null && p.* != 30 { error += 1 }

    // Missing key
    if m.get(String.from_cstr("bar")) != null { error += 1 }

    m.destroy()
    error
}

test_str_key_overwrite() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr("key"), 100)
    m.set(String.from_cstr("key"), 200)

    if m.size() != 1zu { error += 1 }

    p := m.get(String.from_cstr("key"))
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    m.destroy()
    error
}

test_str_key_remove() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr("a"), 1)
    m.set(String.from_cstr("b"), 2)
    m.set(String.from_cstr("c"), 3)

    if !m.remove(String.from_cstr("b")) { error += 1 }
    if m.size() != 2zu { error += 1 }
    if m.contains(String.from_cstr("b")) { error += 1 }
    if !m.contains(String.from_cstr("a")) { error += 1 }
    if !m.contains(String.from_cstr("c")) { error += 1 }

    m.destroy()
    error
}

test_str_key_empty_string() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    m.set(String.from_cstr(""), 42)
    if m.size() != 1zu { error += 1 }

    p := m.get(String.from_cstr(""))
    if p == null { error += 1 }
    if p != null && p.* != 42 { error += 1 }

    // Empty string and non-empty must be different keys
    m.set(String.from_cstr("x"), 99)
    if m.size() != 2zu { error += 1 }

    m.destroy()
    error
}

test_str_key_long_strings() {
    error := 0
    m     := HashMap.create[String.String, Int]()

    // These exceed SSO threshold (14 bytes), exercising big-string hashing
    m.set(String.from_cstr("this is a long string key"), 1)
    m.set(String.from_cstr("another lengthy key value"), 2)

    if m.size() != 2zu { error += 1 }

    p := m.get(String.from_cstr("this is a long string key"))
    if p == null { error += 1 }
    if p != null && p.* != 1 { error += 1 }

    p = m.get(String.from_cstr("another lengthy key value"))
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
