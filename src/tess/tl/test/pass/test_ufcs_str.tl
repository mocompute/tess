#module main
#import <Str.tl>
#import <Array.tl>

// Test: Str via implicit module UFCS
main() {
    error := 0

    s := Str.from_cstr("Hello, World!")

    // Queries — value receiver
    error += if s.len() == 13zu {0} else {1}
    error += if s.is_empty() == false {0} else {1}

    empty := Str.empty()
    error += if empty.is_empty() == true {0} else {1}

    // Comparison — value receiver
    s2 := Str.from_cstr("Hello, World!")
    error += if s.eq(s2) == true {0} else {1}

    // Predicates — value receiver
    error += if s.starts_with(Str.from_cstr("Hello")) == true {0} else {1}
    error += if s.ends_with(Str.from_cstr("World!")) == true {0} else {1}
    error += if s.contains(Str.from_cstr("lo, W")) == true {0} else {1}
    error += if s.contains(Str.from_cstr("xyz")) == false {0} else {1}

    // Transformations — value receiver, returns new Str
    upper := Str.from_cstr("hello").to_upper()
    error += if upper.eq(Str.from_cstr("HELLO")) {0} else {1}

    lower := Str.from_cstr("HELLO").to_lower()
    error += if lower.eq(Str.from_cstr("hello")) {0} else {1}

    trimmed := Str.from_cstr("  hi  ").trim()
    error += if trimmed.eq(Str.from_cstr("hi")) {0} else {1}

    // Cross-module return: split returns Array[Str]
    parts := Str.from_cstr("a,b,c").split(Str.from_cstr(","))
    error += if parts.size == 3zu {0} else {1}
    error += if parts.get(0zu).eq(Str.from_cstr("a")) {0} else {1}
    error += if parts.get(2zu).eq(Str.from_cstr("c")) {0} else {1}
    parts.free()

    // Mutation: push — Ptr receiver, implicit & + implicit module
    greeting := Str.from_cstr("Hello")
    greeting.push(Str.from_cstr(", World!"))
    error += if greeting.eq(Str.from_cstr("Hello, World!")) {0} else {1}

    error
}
