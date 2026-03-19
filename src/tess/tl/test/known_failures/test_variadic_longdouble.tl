// Test CLongDouble -> to_string conversion via variadic ToString trait.
// Currently fails: CLongDouble does not satisfy ToString trait bound —
// trait dispatch doesn't find Float.to_string[T] for CLongDouble.

#import <ToString.tl>

#module main

concat_all(args: ...ToString.ToString) -> String {
    result := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.&.push(args.v.[i])
    }
    result
}

main() {
    x: CLongDouble := 3.14
    s := concat_all(x)

    if !s.eq_cstr("3.14") { c_exit(1) }
    0
}
