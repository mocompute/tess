#module main

// Cross-family: signed literal (0) should not unify with unsigned CSize.

take_size(x: CSize) { x }

main() {
    take_size(0)
    0u
}
