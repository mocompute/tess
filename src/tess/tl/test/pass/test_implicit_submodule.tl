// Test implicit submodule visibility in parent modules.
// In #module Outer, bare "Inner" resolves to "Outer.Inner" for dotted access
// and as a type (auto-collapse).
#module Outer

#module Outer.Inner
InnerData: { value: CInt }
create(v: CInt) -> InnerData { InnerData(value = v) }
get_value(self: Ptr[Const[InnerData]]) -> CInt { self->value }

#module Outer

// Dotted access: Inner.create resolves to Outer.Inner.create
make_inner(v: CInt) -> Inner.InnerData { Inner.create(v) }

// Bare type: Inner as a type resolves to Outer.Inner.Inner (auto-collapse)
// (Note: InnerData is the type here, Inner is not a type itself)

// UFCS through implicit submodule
get_val(d: Inner.InnerData) -> CInt { d.get_value() }

#module main
main() {
    d := Outer.make_inner(10)
    r := Outer.get_val(d)
    // Also verify direct Outer.Inner still works
    d2 := Outer.Inner.create(32)
    r + d2.value - 42
}
