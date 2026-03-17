#module main
#import <String.tl>
#import <Array.tl>

// Test: String via implicit module UFCS
main() {
    error := 0

    s := String.from_cstr("Hello, World!")

    // Queries — value receiver
    error += if s.len() == 13zu {0} else {1}
    error += if s.is_empty() == false {0} else {1}

    empty := String.empty()
    error += if empty.is_empty() == true {0} else {1}

    // Comparison — value receiver
    s2 := String.from_cstr("Hello, World!")
    error += if s.eq(s2) == true {0} else {1}

    // Predicates — value receiver
    error += if s.starts_with(String.from_cstr("Hello")) == true {0} else {1}
    error += if s.ends_with(String.from_cstr("World!")) == true {0} else {1}
    error += if s.contains(String.from_cstr("lo, W")) == true {0} else {1}
    error += if s.contains(String.from_cstr("xyz")) == false {0} else {1}

    // Transformations — value receiver, returns new String
    upper := String.from_cstr("hello").to_upper()
    error += if upper.eq(String.from_cstr("HELLO")) {0} else {1}

    lower := String.from_cstr("HELLO").to_lower()
    error += if lower.eq(String.from_cstr("hello")) {0} else {1}

    trimmed := String.from_cstr("  hi  ").trim()
    error += if trimmed.eq(String.from_cstr("hi")) {0} else {1}

    // Cross-module return: split returns Array[String]
    parts := String.from_cstr("a,b,c").split(String.from_cstr(","))
    error += if parts.size == 3zu {0} else {1}
    error += if parts.get(0zu).eq(String.from_cstr("a")) {0} else {1}
    error += if parts.get(2zu).eq(String.from_cstr("c")) {0} else {1}
    parts.free()

    // Mutation: push — Ptr receiver, implicit & + implicit module
    greeting := String.from_cstr("Hello")
    greeting.push(String.from_cstr(", World!"))
    error += if greeting.eq(String.from_cstr("Hello, World!")) {0} else {1}

    error
}
