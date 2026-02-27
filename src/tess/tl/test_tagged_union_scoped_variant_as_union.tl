#module main

// Test: Scoped variant syntax (Op.A) should be usable where the parent
// tagged union type (Op) is expected.  Currently Op.A produces type Op__A
// instead of Op, so unification with an Op parameter fails.

Op: | A | B

f(op: Op) -> Bool {
    when op {
        a: A { true }
        b: B { false }
    }
}

main() {
    x := Op.A
    r := f(x)
    if r { 0 } else { 1 }
}
