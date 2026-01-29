#module main

T(a): | Some { v: a }
      | None

main() {
    good: T(Int) := T.None()
    0
}
