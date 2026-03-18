#module main

// Adversarial stress test: weak int literals in complex generic scenarios
// that stress post_specialize defaulting and specialization cache keying.

// --- Helpers ---

identity[T](x: T) -> T { x }

Wrapper[T]:                         { v: T           }
make_wrapper[T](x: T) -> Wrapper[T] { Wrapper(v = x) }

MyOpt[T]: | MySome { v: T }
          | MyNone

// --- 1. Same generic specialized with BOTH signed and unsigned weak ints ---
// Must produce two distinct specializations (Int vs UInt).

coerce[T](x: T) -> T { x }

test_signed_vs_unsigned_specializations() {
    error := 0
    // Signed weak int => specializes coerce to Int
    a := coerce(42)
    error += if a == 42 {0} else {1}
    // Unsigned weak int => specializes coerce to UInt (distinct specialization)
    b := coerce(42u)
    error += if b == 42u {0} else {1}
    // Verify they are truly different by using both in the same scope
    c := coerce(100)
    d := coerce(100u)
    error += if c == 100 {0} else {1}
    error += if d == 100u {0} else {1}
    error
}

// --- 2. Weak int in generic struct inside a generic tagged union ---

wrap_in_opt[T](x: T) -> MyOpt[Wrapper[T]] {
    MySome(make_wrapper(x))
}

test_struct_in_tagged_union() {
    error := 0
    // Weak literal 42 flows into Wrapper[T] inside MyOpt[Wrapper[T]]
    opt := wrap_in_opt(42)
    error += when opt {
        s: MySome { if s.v.v == 42 {0} else {1} }
        n: MyNone { 1 }
    }
    // Unsigned version
    opt2 := wrap_in_opt(99u)
    error += when opt2 {
        s: MySome { if s.v.v == 99u {0} else {1} }
        n: MyNone { 1 }
    }
    error
}

// --- 3. Generic higher-order function with closure returning weak int literal ---

apply_thunk[T](f: () -> T) -> T { f() }

transform[T](x: T, f) -> T { f(x) }

test_higher_order_weak_int() {
    error := 0
    // Closure returns weak literal 42; apply_thunk is generic
    r := apply_thunk(() { 42 })
    error += if r == 42 {0} else {1}
    // Unsigned closure
    r2 := apply_thunk(() { 77u })
    error += if r2 == 77u {0} else {1}
    // Transform with weak literal in the lambda
    r3 := transform(10, (x) { x + 1 })
    error += if r3 == 11 {0} else {1}
    // Transform with unsigned
    r4 := transform(10u, (x) { x + 1u })
    error += if r4 == 11u {0} else {1}
    error
}

// --- 4. Multiple levels of generic indirection with weak int ---
// level3 calls level2 calls level1 calls identity, all generic

level1[T](x: T) -> T { identity(x) }
level2[T](x: T) -> T { level1(x)   }
level3[T](x: T) -> T { level2(x)   }

// Plus a version that wraps in a struct at the bottom
level1_box[T](x: T) -> Wrapper[T] { make_wrapper(x) }
level2_box[T](x: T) -> Wrapper[T] { level1_box(x)   }
level3_box[T](x: T) -> Wrapper[T] { level2_box(x)   }

test_deep_indirection() {
    error := 0
    // 3 levels of generic + weak literal
    r := level3(42)
    error += if r == 42 {0} else {1}
    r2 := level3(55u)
    error += if r2 == 55u {0} else {1}
    // 3 levels ending in a generic struct
    w := level3_box(42)
    error += if w.v == 42 {0} else {1}
    w2 := level3_box(99u)
    error += if w2.v == 99u {0} else {1}
    error
}

// --- 5. Generic function with weak int AND concrete Int parameter ---

mixed[T](generic_val: T, concrete_val: Int) -> T {
    // concrete_val is always Int; generic_val's type flows from weak literal
    if concrete_val > 0 { generic_val } else { generic_val }
}

add_and_wrap[T](x: T, offset: Int) -> Wrapper[T] {
    make_wrapper(x)
}

test_mixed_weak_and_concrete() {
    error := 0
    // 42 is weak (defaults to Int via T), 10 is concrete Int
    r := mixed(42, 10)
    error += if r == 42 {0} else {1}
    // 42u is weak unsigned, 10 is concrete Int
    r2 := mixed(42u, 10)
    error += if r2 == 42u {0} else {1}
    // Wrap with mixed params
    w := add_and_wrap(77, 5)
    error += if w.v == 77 {0} else {1}
    w2 := add_and_wrap(77u, 5)
    error += if w2.v == 77u {0} else {1}
    error
}

// --- 6. Weak int through nested Option (builtin generic tagged union) ---

test_weak_int_nested_option() {
    error := 0
    // Some(42) where 42 is weak => Option[Int]
    o := Some(42)
    error += if (s: Some := o else { 0 }, s.v) == 42 {0} else {1}
    // Some(Some(42)) => Option[Option[Int]] with weak int at innermost
    nested := Some(Some(42))
    error += when nested {
        outer: Some {
            when outer.v {
                inner: Some { if inner.v == 42 {0} else {1} }
                n:     None { 1 }
            }
        }
        n: None { 1 }
    }
    // Unsigned single-level through builtin Option
    opt_u := Some(99u)
    error += if (s: Some := opt_u else { 0u }, s.v) == 99u {0} else {1}
    error
}

// --- 7. Weak int literal in generic function that also returns a generic tagged union ---

maybe_wrap[T](x: T, do_wrap: Int) -> MyOpt[T] {
    if do_wrap > 0 { MySome(x) } else { MyNone() }
}

test_generic_returning_tagged_union() {
    error := 0
    // Weak literal 42, generic function returning MyOpt[T]
    r := maybe_wrap(42, 1)
    error += when r {
        s: MySome { if s.v == 42 {0} else {1} }
        n: MyNone { 1 }
    }
    // None path
    r2 := maybe_wrap(42, 0)
    error += when r2 {
        s: MySome { 1 }
        n: MyNone { 0 }
    }
    // Unsigned
    r3 := maybe_wrap(99u, 1)
    error += when r3 {
        s: MySome { if s.v == 99u {0} else {1} }
        n: MyNone { 1 }
    }
    error
}

// --- 8. Stress: call chain where each level uses a different generic container ---

chain_id_to_box[T] (x: T) -> Wrapper[T]        { make_wrapper(identity(x))  }
chain_box_to_opt[T](x: T) -> MyOpt[Wrapper[T]] { MySome(chain_id_to_box(x)) }

test_mixed_container_chain() {
    error := 0
    // Weak 42 -> identity -> Wrapper -> MyOpt
    r := chain_box_to_opt(42)
    error += when r {
        s: MySome { if s.v.v == 42 {0} else {1} }
        n: MyNone { 1 }
    }
    r2 := chain_box_to_opt(88u)
    error += when r2 {
        s: MySome { if s.v.v == 88u {0} else {1} }
        n: MyNone { 1 }
    }
    error
}

// --- Main ---

main() {
    error := 0
    error += test_signed_vs_unsigned_specializations()
    error += test_struct_in_tagged_union()
    error += test_higher_order_weak_int()
    error += test_deep_indirection()
    error += test_mixed_weak_and_concrete()
    error += test_weak_int_nested_option()
    error += test_generic_returning_tagged_union()
    error += test_mixed_container_chain()
    error
}
