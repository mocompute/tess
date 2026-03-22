// Test bare submodule name as type (auto-collapse).
// In #module Parent, bare c"Child" as a type resolves to Parent.Child.Child.
#module Parent

#module Parent.Child
Child:                   { x: CInt      }
create(v: CInt) -> Child { Child(x = v) }

#module Parent

// Bare c"Child" in type position resolves to Parent.Child.Child
wrap (v: CInt)  -> Child { Child.create(v) }
get_x(c: Child) -> CInt  { c.x             }

#module main

main() {
    c := Parent.wrap(42)
    Parent.get_x(c) - 42
}
