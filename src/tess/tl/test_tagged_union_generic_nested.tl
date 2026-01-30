#module main

MyOption(a): | Has { v: a }
             | Empty

main() {
    nested := Has(Has(42))
    0
}
