// Fully generic type alias: assigning None() to aliased Option crashes the compiler.
// `Foo = Option` works for `Some(42)` but segfaults when `None()` is used.

#module App
Foo = Option

#module main

main() {
    n: App.Foo[Int] := None()
    when n { s: Some { 1 } else { 0 } }
}
