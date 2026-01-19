#module main

Either(a, b) : | Left  { v: a }
               | Right { v: b }

main() {
    x: Either(Int, Bool) := Either_Left(v = 42)
    0
}
