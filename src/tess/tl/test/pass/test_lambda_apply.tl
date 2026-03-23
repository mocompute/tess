#module main
apply(f) { f() }

main() {
    a := 0
    l := () { a }
    apply(l)
}
