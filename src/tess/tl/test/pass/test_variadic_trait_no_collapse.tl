// Test that traits whose name does NOT match their module still work
// with the fully qualified form in variadic bounds.
// Auto-collapse must NOT create a bare alias for these.

#import <String.tl>
#import <ToString.tl>

#module Helpers

Describable[T]: { describe(a: T) -> String }

#module Int

describe[T](n: T) -> String {
    s      := to_string(n)
    result := s"["
    result.&.push(s)
    result.&.push(s"]")
    result
}

#module Bool

describe(b: Bool) -> String {
    s      := to_string(b)
    result := s"["
    result.&.push(s)
    result.&.push(s"]")
    result
}

#module main

// Must use fully qualified Helpers.Describable — bare Describable should not exist
join_all(args: ...Helpers.Describable) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.&.push(args.v.[i])
    }
    result
}

main() {
    s1 := join_all(42, true)
    if !s1.eq_cstr("[42][true]") { c_exit(1) }

    s2 := join_all()
    if !s2.is_empty() { c_exit(2) }

    0
}
