#module main

// Type annotation prevents f/1 from being generalised
f(x: Int) { x }

main() {
    f(0.5)                              // error: expected Int
    0
}
