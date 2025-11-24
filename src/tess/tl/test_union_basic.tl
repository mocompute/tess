#module main

Foo : { | the_int : Int | the_float : Float }

main() {
  i := Foo(the_int = 1)
  j := Foo(the_float = 0.5)
  i.the_int - 1
}
