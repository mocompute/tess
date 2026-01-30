#module main

T(a): | Some { v: a }
      | None

unwrap(opt: T(a), default: a) -> a {
    case opt: T {
        s: Some { s.v }
        n: None { default }
    }
}

main() {
    val := Some(42)

    // Testing polymorphism of T and unwrap
    val_float := Some(0.5)
    if unwrap(val_float, 0.0) != 0.5 { return 1 }

    unwrap(val, 0) - 42
}
