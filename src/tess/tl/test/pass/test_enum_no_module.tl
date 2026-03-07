#module main

EnumTag: { tZero, tOne, tTwo }
DupTag:  { tFoo, tBar, tZero }

#ifc
// Use C to force cast of enum to int
int enum_tag_to_int(EnumTag x) { return x; }
#endc

// Type declaration required
c_enum_tag_to_int(x: EnumTag) -> CInt

main() {
    val     := EnumTag.tZero
    val_int := c_enum_tag_to_int(val)
    val_int
}
