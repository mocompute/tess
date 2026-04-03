#module main
#import <Alloc.tl>

// --- helpers for test_apply ---
apply_simple(f) { f() }

// --- helpers for test_arg_annotated ---
apply_annotated(f: (Int) -> Void, y) -> Void {
    f(y)
}

// --- helpers for test_arg_unused ---
apply_unused(f, y) -> Void {
    f(y)
}

// --- helpers for test_inline_argument ---
// applies narrowing cast from Int to CInt
apply_inline(f: (Int) -> Int, x: Int) -> CInt { ci: CInt := f(x) ci }

// --- helpers for test_nested_context ---
apply_nested (f) { f()              }
apply_nested2(f) { apply_nested(f)  }
apply_nested3(f) { apply_nested2(f) }

// --- helpers for test_return_annotation ---
apply_ret(f: (Int) -> Int, x: Int) -> Int { f(x) }

// --- test_basic ---
test_basic() {
    a := 0
    l := () { a }
    l()
}

// --- test_apply ---
test_apply() {
    a := 0
    l := () { a }
    apply_simple(l)
}

// --- test_arg_annotated ---
test_arg_annotated() {
    error := 0
    count := 0

    apply_annotated((x) { count = count + x }, 3)

    if count != 3 { error += 1 }
    error
}

// --- test_arg_unused ---
test_arg_unused() {
    error := 0
    count := 0

    // The bug happens only if `x` does not appear in the body of the lambda.
    apply_unused((x) { count = count + 3 }, 0)

    if count != 3 { error += 1 }
    error
}

// --- test_anon ---
test_anon() {
    (x) { x - 5 } (5)
}

// --- test_attributes ---
test_attributes() {
    error := 0

    // single attribute on lambda
    f := [[my_attr]] (x) { x + 1 }
    error += if f :: [[my_attr]] {0} else {1}
    error += if f :: [[bad]] {1} else {0}

    // multiple attributes on lambda
    g := [[attr1, attr2]] (x) { x * 2 }
    error += if g :: [[attr1]] {0} else {1}
    error += if g :: [[attr2]] {0} else {1}
    error += if g :: [[bad]] {1} else {0}

    // parameterized attribute on lambda
    n := 10
    h := [[heap, grab(n)]] (x) { x + n }
    error += if h :: [[heap]] {0} else {1}
    error += if h :: [[grab(n)]] {0} else {1}
    error += if h :: [[grab]] {0} else {1}

    // lambda attribute combined with parameter attribute
    k := [[my_attr]] ([[param_attr]] x) { x :: [[param_attr]] }
    error += if k :: [[my_attr]] {0} else {1}
    error += if k(42) {0} else {1}

    result: CInt := error
    result
}

// --- test_capture_mutate ---
test_capture_mutate() {
    x: CInt := 1
    f       := (n) { x = x + n }
    f(-1)
    x
}

// --- test_immediate ---
test_immediate() {
    // Note: this is hard to read but it is sensible:
    // lambda    arg    lambda   arg
    // vvvvvvvvv vvvv   vvvvvvvv vv
    (x) { x } (-1) + () { 1 } ()
}

// --- test_inline_argument ---
test_inline_argument() {
    apply_inline((x) { x + 1 }, 5) - 6
}

// --- test_nested_context ---
test_nested_context() {
    val  := true
    afun := () { val }
    foo  := apply_nested3(afun)
    if foo { 0 } else { 1 }
}

// --- test_reassign_into_stack ---
test_reassign_into_stack() {
    x := 1
    f := () { x }
    x = 0
    f()
}

// --- test_return_annotation ---
test_return_annotation() {
    error := 0

    // basic return type annotation
    add := (a: Int, b: Int) -> Int { a + b }
    if add(2, 3) != 5 { error += 1 }

    // return type annotation with no parameter annotations
    inc := (x) -> Int { x + 1 }
    if inc(10) != 11 { error += 1 }

    // return type annotation on closure
    offset  := 100
    shifted := (x: Int) -> Int { x + offset }
    if shifted(5) != 105 { error += 1 }

    // return type annotation on lambda passed as argument
    result := apply_ret((x: Int) -> Int { x * 2 }, 7)
    if result != 14 { error += 1 }

    // void return type
    count := 0
    bump  := (n: Int) -> Void { count = count + n, void }
    bump(3)
    if count != 3 { error += 1 }

    error
}

// --- test_return_annotation_alloc_capture ---
test_return_annotation_alloc_capture() {
    error := 0

    // alloc + capture with return type annotation
    base     := 10
    add_base := [[alloc, capture(base)]] (x: Int) -> Int { x + base }
    if add_base(5) != 15 { error += 1 }

    // alloc + capture with return type annotation, no param annotations
    multiplier := 3
    mul        := [[alloc, capture(multiplier)]] (x) -> Int { x * multiplier }
    if mul(4) != 12 { error += 1 }

    // alloc + capture with void return type (uses pointer for mutation visibility)
    acc        := 0
    acc_p      := acc.&
    add_to_acc := [[alloc, capture(acc_p)]] (n: Int) -> Void { acc_p.* += n }
    add_to_acc(7)
    if acc != 7 { error += 1 }

    error
}

// --- main ---
main() {
    error := 0
    error += test_basic()
    error += test_apply()
    error += test_arg_annotated()
    error += test_arg_unused()
    error += test_anon()
    error += test_attributes()
    error += test_capture_mutate()
    error += test_immediate()
    error += test_inline_argument()
    error += test_nested_context()
    error += test_reassign_into_stack()
    error += test_return_annotation()
    error += test_return_annotation_alloc_capture()
    error
}
