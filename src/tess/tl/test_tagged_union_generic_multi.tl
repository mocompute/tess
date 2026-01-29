#module main

Either(a, b): | Left { v: a }
              | Right { v: b }

main() {
    x: Either(Int, Bool) := Either.Left(v = 42)
    0
}
