#module M

Triple[T]: | A { v: T } | B { v: T } | C { v: T }

#module main

test() -> M.Triple[Int] {
    val := M.A(1)
    s   := try val
    M.A(s.v)
}

main() {
    test()
    0
}
