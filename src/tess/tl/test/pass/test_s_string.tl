#module main
#import <String.tl>
#import <cstdio.tl>

main() {
    errors := 0

    // s"..." creates a String
    s := s"hello"
    if s.len() != 5 { errors += 1 }

    // Can call String methods on s"..."
    if !s.eq(String.from_cstr("hello")) { errors += 1 }

    // Empty s-string
    e := s""
    if !e.is_empty() { errors += 1 }

    // Plain "..." still works as C string
    cs := "world"
    s2 := String.from_cstr(cs)
    if s2.len() != 5 { errors += 1 }

    // s"..." and plain strings coexist
    combined := String.cat(s"hello", s" world")
    if combined.len() != 11 { errors += 1 }
    if !combined.eq(s"hello world") { errors += 1 }

    // s-string with escape sequences
    nl := s"a\nb"
    if nl.len() != 3 { errors += 1 }

    errors
}
