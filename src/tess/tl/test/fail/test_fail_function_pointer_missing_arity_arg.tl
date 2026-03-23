#module main

add(a, b) { a + b }
apply(f, x, y) { f(x, y) }

main() {
    apply(add, 1, 2)
}
