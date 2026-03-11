#module main

main() {
    x := 42
    y := 10
    mp: Ptr[Int] := y.&
    cp: Ptr[Const[Int]] := x.&
    mp = cp                      // error: cannot strip const in reassignment
    0
}
