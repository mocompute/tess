#module main

T(a) = | Some { v: a }
       | None

main() {
    bad: T(Int) := T_None()
    0
}
