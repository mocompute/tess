#module main
#import <Alloc.tl>
#import <String.tl>

// Partially-annotated generic lambda closure called at different argument
// types.  The return type is fixed (Int) but the param type varies per
// call site.  The specializer creates multiple instances but there is only
// one closure binding, so the transpiled code references closure structs
// that don't exist.

main() {
    error := 0

    special := 42
    constant := [[alloc, capture(special)]] (x) -> Int { special }

    if constant(1.0) != 42 { error += 1 }
    if constant("hello") != 42 { error += 1 }
    if constant(0) != 42 { error += 1 }

    error
}
