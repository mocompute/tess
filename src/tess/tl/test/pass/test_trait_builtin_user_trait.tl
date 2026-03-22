// Test that builtin types can satisfy user-defined trait bounds.
// User code defines a trait and implements it in #module Int, #module Bool, etc.
// The compiler's check_trait_bound_ must use function lookup for user-defined traits
// on builtins, rather than the hardcoded capability table.

#import <String.tl>

#module Describe

Describe[T]: {
    describe(x: T) -> String
}

#module Int
describe(n) -> String { String.from_int(n) }

#module Bool
describe(b) -> String { String.from_bool(b) }

#module Float
describe(f) -> String { String.from_float(f) }

#module main

show[T: Describe](x: T) -> String { x.describe() }

main() {
    error := 0

    // Int satisfies Describe
    s := show(42)
    error += if String.eq(s, String.from_cstr(c"42")) { 0 } else { 1 }

    // Bool satisfies Describe
    sb := show(true)
    error += if String.eq(sb, String.from_cstr(c"true")) { 0 } else { 1 }

    // Float satisfies Describe
    sf := show(3.14)
    error += if String.len(sf) > 0 { 0 } else { 1 }

    error
}
