#module main

// renamed due to new conflict with builtin Some/None
T[a]: | MySome { v: a }
      | MyNone

main() {
    good: T[Int] := MyNone()
    0
}
