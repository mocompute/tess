#module main

// renamed due to new conflict with builtin Some/None
T(a): | MySome { v: a }
      | MyNone

main() {
    good := MySome(123)

    res := case good: T {
        s: MySome { s.v }
        n: MyNone { 0 }
    }
    res - 123
}
