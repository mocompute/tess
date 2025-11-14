#module main

EnumTag : {tZero, tOne, tTwo}
DupTag :  {tFoo, tBar, tZero }

main() {
  val := EnumTag.tZero
  val_int : CInt := val
  val_int
}
