#module main
#import <String.tl>
#import <cstdio.tl>

main() {
    errors := 0

    // "..." creates a String
    s := "hello"
    if s.len() != 5 { errors += 1 }

    // Can call String methods on "..."
    if !s.eq(String.from_cstr(c"hello")) { errors += 1 }

    // Empty s-string
    e := ""
    if !e.is_empty() { errors += 1 }

    // Plain c"..." still works as C string
    cs := c"world"
    s2 := String.from_cstr(cs)
    if s2.len() != 5 { errors += 1 }

    // "..." and plain strings coexist
    combined := String.cat("hello", " world")
    if combined.len() != 11 { errors += 1 }
    if !combined.eq("hello world") { errors += 1 }

    // s-string with escape sequences
    nl := "a\nb"
    if nl.len() != 3 { errors += 1 }

    errors
}
