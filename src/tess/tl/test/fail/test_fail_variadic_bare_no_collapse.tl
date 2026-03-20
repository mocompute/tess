// Bare trait name must be rejected when trait name differs from module name.
// Describable is defined in #module Helpers, so ...Describable should fail —
// only ...Helpers.Describable is valid.

#import <String.tl>
#import <ToString.tl>

#module Helpers

Describable[T]: { describe(a: T) -> String }

#module Int

describe[T](n: T) -> String {
    to_string(n)
}

#module main

// This should fail: bare Describable does not exist, only Helpers.Describable
join_all(args: ...Describable) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        result.&.push(args.v.[i])
    }
    result
}

main() {
    join_all(42)
    0
}
