// Else binding requires a two-variant union. Three or more variants should fail.
#module M

Triple[T]: | A { v: T } | B { v: T } | C { v: T }

#module main

test() -> CInt {
    val: M.Triple[CInt] := M.A(1)
    a: A := val else other { return 1 }
    a.v
}

main() {
    test()
}
