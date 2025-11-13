#module Foo

EnumTag : {tZero, tOne, tTwo}
DupTag :  {tFoo, tBar, tZero}

#module main

main() {
  val1 := Foo.EnumTag.tZero
  val2 := Foo.DupTag.tZero

  // cast enums to integer type
  val1int : Int := val1
  val2int : Int := val2
  val3 := val1int + val2int - 2

}
