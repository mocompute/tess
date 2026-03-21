// Fully generic type alias: Foo = Option (no type params bound).
// The alias can be used with type arguments: App.Foo[Int].

#module App
Foo = Option

#module main

main() {
    x: App.Foo[Int] := Some(42)
    when x { s: Some { s.v - 42 } else { 1 } }
}
