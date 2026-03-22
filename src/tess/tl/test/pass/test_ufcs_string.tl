#module main
#import <String.tl>
#import <Array.tl>

// Test: String via implicit module UFCS
main() {
    error := 0

    s := String.from_cstr(c"Hello, World!")

    // Queries — value receiver
    error += if s.len() == 13zu {0} else {1}
    error += if s.is_empty() == false {0} else {1}

    empty := String.empty()
    error += if empty.is_empty() == true {0} else {1}

    // Comparison — value receiver
    s2 := String.from_cstr(c"Hello, World!")
    error += if s.eq(s2) == true {0} else {1}

    // Predicates — value receiver
    error += if s.starts_with(String.from_cstr(c"Hello")) == true {0} else {1}
    error += if s.ends_with(String.from_cstr(c"World!")) == true {0} else {1}
    error += if s.contains(String.from_cstr(c"lo, W")) == true {0} else {1}
    error += if s.contains(String.from_cstr(c"xyz")) == false {0} else {1}

    // Transformations — value receiver, returns new String
    upper := String.from_cstr(c"hello").to_upper()
    error += if upper.eq(String.from_cstr(c"HELLO")) {0} else {1}

    lower := String.from_cstr(c"HELLO").to_lower()
    error += if lower.eq(String.from_cstr(c"hello")) {0} else {1}

    trimmed := String.from_cstr(c"  hi  ").trim()
    error += if trimmed.eq(String.from_cstr(c"hi")) {0} else {1}

    // Cross-module return: split returns Array[String]
    parts := String.from_cstr(c"a,b,c").split(String.from_cstr(c","))
    error += if parts.size == 3zu {0} else {1}
    error += if parts.get(0zu).eq(String.from_cstr(c"a")) {0} else {1}
    error += if parts.get(2zu).eq(String.from_cstr(c"c")) {0} else {1}
    parts.free()

    // Mutation: push — Ptr receiver, implicit & + implicit module
    greeting := String.from_cstr(c"Hello")
    greeting.push(String.from_cstr(c", World!"))
    error += if greeting.eq(String.from_cstr(c"Hello, World!")) {0} else {1}

    error
}
