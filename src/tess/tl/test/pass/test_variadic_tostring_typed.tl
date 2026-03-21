// Test explicitly-typed arguments with variadic ToString trait bound.
// Verifies that generic trait functions (e.g. Float.to_string[T]) correctly
// instantiate for nullary builtin types.

#import <ToString.tl>

#module main

concat_all(args: ...ToString) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.push(args.v.[i])
    }
    result
}

main() {
    x: CLongDouble  := 3.14
    y: CFloat       := 2.0
    z: CInt         := 42
    w: CUnsignedInt := 7

    s := concat_all(x, y, z, w)

    if !s.eq_cstr("3.142427") { c_exit(1) }
    0
}
