#module Foo

EnumTag : {tZero, tOne, tTwo}
DupTag :  {tFoo, tBar, tZero }

#module main

main() {
  val := Foo.EnumTag.tZero
  val
}
