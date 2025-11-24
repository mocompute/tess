#module main

Foo : { | int : Int | float : Float }

main() {
  i := Foo(int = 1)
  i.int - 1
}
