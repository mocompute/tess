#module main

Op: | A | B

f(op: Op) -> Bool {
    when op {
        a: A { true }
        b: B { false }
    }
}

main() {
    error := 0

    // Bare symbol (no parens): Op.A should produce Op, not Op__A
    x := Op.A
    r := f(x)
    error += if r { 0 } else { 1 }

    error
}
