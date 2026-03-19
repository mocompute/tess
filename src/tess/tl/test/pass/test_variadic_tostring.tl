// Test the ToString trait with variadic functions.

#module main
#import <ToString.tl>

concat_all(args: ...ToString.ToString) -> String {
    result := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.&.push(args.v.[i])
    }
    result
}

main() {
    // Basic: int + bool
    s1 := concat_all(42, true)
    if !s1.eq_cstr("42true") { c_exit(1) }

    // Multiple types: int + bool + float
    s2 := concat_all(100, false, 3.14)
    if !s2.eq_cstr("100false3.14") { c_exit(2) }

    // String argument
    hello := String.from_cstr("hello")
    s3 := concat_all(hello)
    if !s3.eq_cstr("hello") { c_exit(3) }

    // Mixed: String + int
    s4 := concat_all(hello, 42)
    if !s4.eq_cstr("hello42") { c_exit(4) }

    // Zero variadic args
    s5 := concat_all()
    if !s5.is_empty() { c_exit(5) }

    0
}
