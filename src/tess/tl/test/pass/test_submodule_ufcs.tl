// Test UFCS on types defined in submodules.
#module Parent

#module Parent.Child
MyType: { x: CInt }
create(v: CInt) -> MyType { MyType(x = v) }
get_x(self: Ptr[Const[MyType]]) -> CInt { self->x }

#module main
main() {
    v := Parent.Child.create(42)
    v.get_x() - 42
}
