#module main

accept_cptrdiff(x: CPtrDiff) -> CPtrDiff { x }

main() {
    x: Int := 42
    accept_cptrdiff(x)
    0
}
