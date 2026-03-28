#module main

// renamed due to new conflict with builtin Some/None
MyOption[a]: | MySome { value: a }
             | MyNone

main() {
    good := MySome(123)

    res := when good {
        s: MySome { s.value }
        n: MyNone { 0 }
    }
    res - 123
}
