// Test that bare trait names work in variadic bounds via auto-collapse.
// e.g. ...ToString instead of ...ToString.ToString

#module main
#import <ToString.tl>
#import <Hash.tl>

concat_all(args: ...ToString) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.&.push(args.v.[i])
    }
    result
}

sum_hashes(args: ...Hash) -> CSize {
    h: CSize := 0
    i: CSize := 0
    while i < args.size, i += 1 {
        h = h + args.v.[i]
    }
    h
}

main() {
    // Bare ...ToString works
    s1 := concat_all(42, true)
    if !s1.eq_cstr("42true") { c_exit(1) }

    s2 := concat_all()
    if !s2.is_empty() { c_exit(2) }

    hello := s"hello"
    s3    := concat_all(hello, 99)
    if !s3.eq_cstr("hello99") { c_exit(3) }

    // Bare ...Hash works
    h := sum_hashes(1, 2, 3)
    if h == 0 { c_exit(4) }

    0
}
