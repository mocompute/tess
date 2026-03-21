// Adversarial test for 762b0784: nested variadic calls with non-collapsed traits.
// A variadic function bounded by a non-collapsed trait (Helpers.Describable)
// internally calls another variadic function (concat_all with ...ToString).
// This tests nested variadic specialization when the outer trait name
// differs from its module name.

#import <String.tl>
#import <ToString.tl>

#module Helpers

Describable[T]: { describe(a: T) -> String }

#module Int

describe[T](n: T) -> String {
    s      := to_string(n)
    result := s"<"
    result.push(s)
    result.push(s">")
    result
}

#module Bool

describe(b: Bool) -> String {
    s      := to_string(b)
    result := s"<"
    result.push(s)
    result.push(s">")
    result
}

#module main

// Inner variadic: uses auto-collapsed ...ToString (trait name == module name)
concat_all(args: ...ToString) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.push(args.v.[i])
    }
    result
}

// Outer variadic: uses non-collapsed ...Helpers.Describable
// Internally calls the inner variadic concat_all
describe_and_join(args: ...Helpers.Describable) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        if i > 0 { result.push(s",") }
        result.push(args.v.[i])
    }
    // Also call inner variadic to verify nesting works
    suffix := concat_all(args.size)
    result.push(suffix)
    result
}

main() {
    error := 0

    // Single argument
    s1 := describe_and_join(42)
    if !s1.eq_cstr("<42>1") { error += 1 }

    // Multiple arguments of different types
    s2 := describe_and_join(10, true)
    if !s2.eq_cstr("<10>,<true>2") { error += 1 }

    // Zero arguments
    s3 := describe_and_join()
    if !s3.eq_cstr("0") { error += 1 }

    error
}
