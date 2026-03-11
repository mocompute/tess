#module main

get_mut_ptr(p: Ptr[Const[Int]]) -> Ptr[Int] {
    return p                     // error: cannot strip const in return
}

main() {
    x := 42
    get_mut_ptr(x.&)
    0
}
