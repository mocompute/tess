// Test: trait auto-collapse for a trait defined in a submodule.
// The symbol_module_matches_original() function handles dotted module paths
// (Foo.Bar__Bar -> ends_with c".Bar"). This tests that a trait defined in
// a submodule can be used in a variadic bound via the dotted module name.

#import <String.tl>

#module Util

#module Util.Describe

Describe[T]: { describe(x: T) -> String }

#module Int
describe(x: Int) -> String { String.from_int(x) }

#module Bool
describe(x: Bool) -> String { if x { "yes" } else { "no" } }

#module main

concat_descriptions(args: ...Util.Describe) -> String {
    result   := String.empty()
    i: CSize := 0
    while i < args.size, i += 1 {
        if i > 0 { result.push(",") }
        result.push(args.data.[i])
    }
    result
}

main() {
    error := 0

    s1 := concat_descriptions(42, true)
    error += if s1.eq_cstr(c"42,yes") { 0 } else { 1 }

    s2 := concat_descriptions(0, false)
    error += if s2.eq_cstr(c"0,no") { 0 } else { 1 }

    s3 := concat_descriptions()
    error += if s3.is_empty() { 0 } else { 1 }

    error
}
