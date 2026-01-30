#module main

T(a): | Has { v: a }
      | Empty

make_has(x) {
    Has(x)
}

main() {
    val := make_has(42)
    0
}
