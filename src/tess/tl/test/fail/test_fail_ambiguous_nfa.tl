#module main

Foo: {val: CInt}

id(n) { n }

main() {
    foo := Foo(val = 10)
    x   := foo.val                      // parser ambiguity: missing comma or semicolon before next expression
    (x - 10)
}
