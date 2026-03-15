// Test implicit submodule with tagged unions, UFCS, and synopsis.
// Mirrors Cmdline.tl patterns: parent module uses Child.Variant, Child.func.
#module Parent

#module Parent.Child
Value: | IntVal { v: CInt }
       | Empty

create(v: CInt) -> Value { IntVal(v = v) }
get(val: Value) -> CInt {
    when val {
        i: IntVal { i.v }
        e: Empty { 0 }
    }
}

#module Parent

// Synopsis using implicit submodule dotted type
wrap(v: CInt) -> Child.Value
wrap(v) { Child.create(v) }

// Use implicit submodule variant constructors
make_empty() -> Child.Value { Child.Empty }

// UFCS on submodule type
extract(val: Child.Value) -> CInt { val.get() }

#module main
main() {
    v := Parent.wrap(42)
    e := Parent.make_empty()
    Parent.extract(v) + Parent.extract(e) - 42
}
