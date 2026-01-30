#module main

Either(a, b): | Left { v: a }
              | Right { v: b }

main() {
    x: Either(Int, Bool) := Left(42)
    0
}
