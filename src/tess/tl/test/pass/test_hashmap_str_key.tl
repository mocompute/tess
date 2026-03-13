#module main
#import <HashMap.tl>
#import <Str.tl>
#import <Array.tl>

test_str_key_basic() {
    error := 0
    m     := HashMap.create[Str.Str, Int]()

    HashMap.set(m, Str.from_cstr("hello"), 10)
    HashMap.set(m, Str.from_cstr("world"), 20)
    HashMap.set(m, Str.from_cstr("foo"), 30)

    if HashMap.size(m) != 3zu { error += 1 }

    p := HashMap.get(m, Str.from_cstr("hello"))
    if p == null { error += 1 }
    if p != null && p.* != 10 { error += 1 }

    p = HashMap.get(m, Str.from_cstr("world"))
    if p == null { error += 1 }
    if p != null && p.* != 20 { error += 1 }

    p = HashMap.get(m, Str.from_cstr("foo"))
    if p == null { error += 1 }
    if p != null && p.* != 30 { error += 1 }

    // Missing key
    if HashMap.get(m, Str.from_cstr("bar")) != null { error += 1 }

    HashMap.destroy(m)
    error
}

test_str_key_overwrite() {
    error := 0
    m     := HashMap.create[Str.Str, Int]()

    HashMap.set(m, Str.from_cstr("key"), 100)
    HashMap.set(m, Str.from_cstr("key"), 200)

    if HashMap.size(m) != 1zu { error += 1 }

    p := HashMap.get(m, Str.from_cstr("key"))
    if p == null { error += 1 }
    if p != null && p.* != 200 { error += 1 }

    HashMap.destroy(m)
    error
}

test_str_key_remove() {
    error := 0
    m     := HashMap.create[Str.Str, Int]()

    HashMap.set(m, Str.from_cstr("a"), 1)
    HashMap.set(m, Str.from_cstr("b"), 2)
    HashMap.set(m, Str.from_cstr("c"), 3)

    if !HashMap.remove(m, Str.from_cstr("b")) { error += 1 }
    if HashMap.size(m) != 2zu { error += 1 }
    if HashMap.contains(m, Str.from_cstr("b")) { error += 1 }
    if !HashMap.contains(m, Str.from_cstr("a")) { error += 1 }
    if !HashMap.contains(m, Str.from_cstr("c")) { error += 1 }

    HashMap.destroy(m)
    error
}

test_str_key_empty_string() {
    error := 0
    m     := HashMap.create[Str.Str, Int]()

    HashMap.set(m, Str.from_cstr(""), 42)
    if HashMap.size(m) != 1zu { error += 1 }

    p := HashMap.get(m, Str.from_cstr(""))
    if p == null { error += 1 }
    if p != null && p.* != 42 { error += 1 }

    // Empty string and non-empty must be different keys
    HashMap.set(m, Str.from_cstr("x"), 99)
    if HashMap.size(m) != 2zu { error += 1 }

    HashMap.destroy(m)
    error
}

test_str_key_long_strings() {
    error := 0
    m     := HashMap.create[Str.Str, Int]()

    // These exceed SSO threshold (14 bytes), exercising big-string hashing
    HashMap.set(m, Str.from_cstr("this is a long string key"), 1)
    HashMap.set(m, Str.from_cstr("another lengthy key value"), 2)

    if HashMap.size(m) != 2zu { error += 1 }

    p := HashMap.get(m, Str.from_cstr("this is a long string key"))
    if p == null { error += 1 }
    if p != null && p.* != 1 { error += 1 }

    p = HashMap.get(m, Str.from_cstr("another lengthy key value"))
    if p == null { error += 1 }
    if p != null && p.* != 2 { error += 1 }

    HashMap.destroy(m)
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
