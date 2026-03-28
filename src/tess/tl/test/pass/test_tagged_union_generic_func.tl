#module main

// renamed due to new conflict with builtin Some/None
T[a]: | MySome { value: a }
      | MyNone

my_unwrap[a](opt: T[a], default: a) -> a {
    when opt {
        s: MySome { s.value }
        n: MyNone { default }
    }
}

main() {
    val := MySome(42)

    // Testing polymorphism of T and unwrap
    val_float := MySome(0.5)
    if my_unwrap(val_float, 0.0) != 0.5 { return 1 }

    my_unwrap(val, 0) - 42
}
