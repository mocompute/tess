// Test cross-module variadic functions with the same base name.
// Two modules each define format_all(args: ...Hash.Hash) — verifies
// that module name mangling prevents collision in the variadic_symbols map.

#import <Hash.tl>

#module Foo

format_all(args: ...Hash.Hash) -> CSize {
    result: CSize := 100
    i := 0
    while i < args.size, i = i + 1 {
        result = result + args.v.[i]
    }
    result
}

#module Bar

format_all(args: ...Hash.Hash) -> CSize {
    result: CSize := 200
    i := 0
    while i < args.size, i = i + 1 {
        result = result + args.v.[i]
    }
    result
}

#module main

main() {
    a := Foo.format_all(1, 2, 3)
    b := Bar.format_all(1, 2, 3)

    if b - a != 100 { c_exit(1) }
    0
}
