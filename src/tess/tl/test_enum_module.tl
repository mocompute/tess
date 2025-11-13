#module Foo

EnumTag : {tZero, tOne, tTwo}
DupTag :  {tFoo, tBar, tZero }

#module main

f(x : Foo.EnumTag) { x }

main() {
  val := f(Foo.EnumTag.tZero)
  val
}
