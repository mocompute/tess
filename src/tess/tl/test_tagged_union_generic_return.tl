#module main

T(a) = | Some { v: a }
       | None

make_some(x: Int): T(Int) {
    T_Some(v = x)
}

main() {
    val := make_some(42)
    0
}
