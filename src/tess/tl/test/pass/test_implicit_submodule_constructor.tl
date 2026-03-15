// Test bare submodule type constructor in expression position.
// In #module Parent, bare "Child(...)" resolves to Parent.Child.Child(...).
#module Parent

#module Parent.Child
Child: { x: CInt, y: CInt }

#module Parent
make() -> Child { Child(x = 10, y = 20) }

#module main
main() {
    c := Parent.make()
    c.x + c.y - 30
}
