#module main

mutate(p: Ptr[Int]) {
    p.* = 99
    0
}

main() {
    x: Const[Int] := 42
    mutate(x.&)
    0
}
