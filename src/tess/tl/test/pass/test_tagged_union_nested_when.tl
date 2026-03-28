#module main

// renamed due to new conflict with builtin Some/None
MyOption[a]: | MySome { value: a }
             | MyNone

main() {
    outer := MySome(MySome(42))

    // Nested when: match outer, then match inner
    result := when outer {
        s: MySome {
            inner := s.value
            when inner {
                s2: MySome { s2.value }
                n:  MyNone { 0 }
            }
        }
        n: MyNone { 0 }
    }
    result - 42
}
