#module main

// Regression: map_option with a lambda that changes the type (Int -> Float)
// fails because the generic specialization system doesn't create separate
// specializations when only the untyped parameter `f` differs in type.

map_option[T, U](opt: Option[T], f) -> Option[U] {
    when opt {
        s: Some { Some(f(s.v)) }
        n: None { None() }
    }
}

main() {
    error := 0

    // Same-type map works: Int -> Int
    opt := Some(3)
    mapped := map_option(opt, (x) { x * 2 })
    error += if unwrap(mapped, 0) == 6 {0} else {1}

    // Cross-type map: Int -> Float (triggers conflicting types)
    int_opt := Some(5)
    mapped_f := map_option(int_opt, (x) { x * 1.5 })
    error += when mapped_f {
        s: Some { if s.v == 7.5 {0} else {1} }
        n: None { 1 }
    }

    error
}
