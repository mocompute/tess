#module main

T(a) : | Some { v: a }
       | None

make_some(x) {
    T.Some(v = x)
}

main() {
    val := make_some(42)
    0
}
