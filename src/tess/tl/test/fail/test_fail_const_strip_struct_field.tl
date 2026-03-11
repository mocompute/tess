#module main

Holder: { p: Ptr[Int] }

main() {
    x := 42
    cp: Ptr[Const[Int]] := x.&
    h := Holder(p = cp)          // error: cannot strip const from field
    0
}
