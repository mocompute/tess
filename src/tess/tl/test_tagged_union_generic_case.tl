#module main

// renamed due to new conflict with builtin Some/None
MyOption[a]: | MySome { v: a }
             | MyNone

main() {
    good := MySome(123)

    res := case good: MyOption {
        s: MySome { s.v }
        n: MyNone { 0 }
    }
    res - 123
}
