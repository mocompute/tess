// Test that dotted type names in synopsis declarations within reopened modules
// resolve correctly (left-to-right, not right-recursive).
#module Outer

#module Outer.Inner
InnerData: { value: CInt }
make(v: CInt) -> InnerData { InnerData(value = v) }

#module Outer
// Synopsis uses a 3-level dotted type name: Outer.Inner.InnerData
wrap(d: Outer.Inner.InnerData) -> CInt
wrap(d) { d.value }

get_inner() -> Outer.Inner.InnerData
get_inner() { Outer.Inner.make(42) }

#module main
main() {
    d := Outer.Inner.make(10)
    r := Outer.wrap(d)
    d2 := Outer.get_inner()
    r + d2.value - 52
}
