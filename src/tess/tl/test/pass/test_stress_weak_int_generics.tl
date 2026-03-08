#module main

// Stress test: weak int literals flowing through generic functions,
// structs, and tagged unions, exercising post_specialize defaulting.

// --- Generic helpers ---

identity[T](x: T) -> T { x }
wrap[T](x: T) -> T { x }
double[T](x: T) -> T { x + x }

Pair[T]: { first: T, second: T }
make_pair[T](a: T, b: T) -> Pair[T] { Pair(first = a, second = b) }

Box[T]: { v: T }
make_box[T](x: T) -> Box[T] { Box(v = x) }

MyOpt[T]: | MySome { v: T }
           | MyNone

// --- 1. Generic function called with weak int literals ---
test_generic_with_weak_literal() {
    error := 0
    // 42 is a weak signed literal; identity is generic => must default to Int
    r := identity(42)
    error += if r == 42 {0} else {1}
    // 100u is a weak unsigned literal => must default to UInt
    r2 := identity(100u)
    error += if r2 == 100u {0} else {1}
    error
}

// --- 2. Nested generic calls: outer(inner(42)) ---
test_nested_generic_calls() {
    error := 0
    // Both identity and wrap are generic; 42 is weak
    r := identity(wrap(42))
    error += if r == 42 {0} else {1}
    // Triple nesting
    r2 := identity(wrap(identity(99)))
    error += if r2 == 99 {0} else {1}
    // Unsigned variant
    r3 := wrap(identity(55u))
    error += if r3 == 55u {0} else {1}
    error
}

// --- 3. Store weak int in generic struct ---
test_weak_int_in_generic_struct() {
    error := 0
    b := make_box(42)
    error += if b.v == 42 {0} else {1}
    b2 := make_box(77u)
    error += if b2.v == 77u {0} else {1}
    error
}

// --- 4. Multiple weak int literals that must unify in a generic call ---
test_pair_weak_ints() {
    error := 0
    p := make_pair(1, 2)
    error += if p.first == 1 {0} else {1}
    error += if p.second == 2 {0} else {1}
    // Unsigned pair
    p2 := make_pair(10u, 20u)
    error += if p2.first == 10u {0} else {1}
    error += if p2.second == 20u {0} else {1}
    error
}

// --- 5. Weak unsigned literals through generics ---
test_unsigned_through_generics() {
    error := 0
    r := double(5u)
    error += if r == 10u {0} else {1}
    r2 := identity(wrap(255u))
    error += if r2 == 255u {0} else {1}
    error
}

// --- 6. Generic tagged union constructed with weak int literals ---
test_tagged_union_weak_int() {
    error := 0
    // MySome(42) where MySome is generic => T must default to Int
    opt := MySome(42)
    error += when opt {
        s: MySome { if s.v == 42 {0} else {1} }
        n: MyNone { 1 }
    }
    // Unsigned variant
    opt2 := MySome(99u)
    error += when opt2 {
        s: MySome { if s.v == 99u {0} else {1} }
        n: MyNone { 1 }
    }
    // Builtin Option with weak int
    opt3 := Some(123)
    error += if unwrap(opt3, 0) == 123 {0} else {1}
    error
}

// --- 7. Generic function called from another generic function with weak int ---

inner_generic[T](x: T) -> T { x }

outer_generic[T](x: T) -> T {
    // Call another generic with the same type parameter
    inner_generic(x)
}

// And a version that introduces a fresh weak literal inside a generic body
outer_with_literal[T](x: T) -> Pair[T] {
    // Both x and the return flow through generic specialization
    make_pair(x, x)
}

test_generic_calls_generic() {
    error := 0
    // Weak int flows: main -> outer_generic -> inner_generic
    r := outer_generic(42)
    error += if r == 42 {0} else {1}
    r2 := outer_generic(10u)
    error += if r2 == 10u {0} else {1}
    // Generic calling generic with struct return
    p := outer_with_literal(7)
    error += if p.first == 7 {0} else {1}
    error += if p.second == 7 {0} else {1}
    error
}

// --- 8. Chain of 3+ generic calls, each passing weak int through ---

step1[T](x: T) -> T { identity(x) }
step2[T](x: T) -> T { step1(x) }
step3[T](x: T) -> T { step2(x) }
step4[T](x: T) -> T { step3(x) }

test_deep_generic_chain() {
    error := 0
    // Weak literal 42 flows through 4 levels of generic indirection
    r := step4(42)
    error += if r == 42 {0} else {1}
    // Unsigned through 4 levels
    r2 := step4(77u)
    error += if r2 == 77u {0} else {1}
    // Nested call at call site too: step4(identity(33))
    r3 := step4(identity(33))
    error += if r3 == 33 {0} else {1}
    error
}

// --- Main ---

main() {
    error := 0
    error += test_generic_with_weak_literal()
    error += test_nested_generic_calls()
    error += test_weak_int_in_generic_struct()
    error += test_pair_weak_ints()
    error += test_unsigned_through_generics()
    error += test_tagged_union_weak_int()
    error += test_generic_calls_generic()
    error += test_deep_generic_chain()
    error
}
