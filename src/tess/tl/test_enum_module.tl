#module Foo

EnumTag : {tZero, tOne, tTwo}
DupTag :  {tFoo, tBar, tZero }

#module main

#ifc
// Use C to force cast of enum to int: note the mangled C type name `Foo__EnumTag`
int enum_tag_to_int(Foo__EnumTag x) { return x; }
#endc

// Type declaration required
c_enum_tag_to_int(x: Foo.EnumTag) -> CInt

f(x : Foo.EnumTag) { x }

main() {
  val := f(Foo.EnumTag.tZero)
  val_int := c_enum_tag_to_int(val)
  val_int
}
