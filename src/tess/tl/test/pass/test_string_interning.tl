#module main
#import <String.tl>

main() {
    error := 0

    // SSO path: short strings (<=14 bytes)
    s1 := "hello"
    error += if s1.len() == 5 { 0 } else { 1 }
    error += if s1.eq("hello") { 0 } else { 1 }

    // Empty string
    e := ""
    error += if e.len() == 0 { 0 } else { 1 }
    error += if e.is_empty() { 0 } else { 1 }

    // Exactly 14 bytes (SSO boundary)
    s14 := "01234567890123"
    error += if s14.len() == 14 { 0 } else { 1 }
    error += if s14.eq("01234567890123") { 0 } else { 1 }

    // Big path: 15 bytes (just over SSO threshold)
    s15 := "012345678901234"
    error += if s15.len() == 15 { 0 } else { 1 }
    error += if s15.eq("012345678901234") { 0 } else { 1 }

    // Longer big string
    long := "this is a long string exceeding sso limit"
    error += if long.len() == 41 { 0 } else { 1 }

    // Deduplication: same big string used twice
    dup1 := "this is a long string exceeding sso limit"
    error += if dup1.eq(long) { 0 } else { 1 }

    // Escape sequences: \n counts as 1 decoded byte
    esc := "a\nb"
    error += if esc.len() == 3 { 0 } else { 1 }

    // Escape sequences in SSO
    esc2 := "tab\there"
    error += if esc2.len() == 8 { 0 } else { 1 }

    // cstr works on interned strings
    cs := String.cstr(s1.&)
    error += if cs != null { 0 } else { 1 }

    // Concatenation with interned strings
    cat := String.cat("hello", " world")
    error += if cat.eq("hello world") { 0 } else { 1 }

    // Interned string equals dynamically constructed string
    dyn := String.from_cstr(c"hello")
    error += if s1.eq(dyn) { 0 } else { 1 }

    error
}
