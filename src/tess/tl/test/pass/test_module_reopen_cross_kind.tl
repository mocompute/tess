// Function and type with the same user-visible name in a module.
// Functions are arity-mangled, so they occupy a separate namespace from types.
#module M

f: { value: CInt }

#module M

f(x: CInt) -> CInt { s := f(value = x); s.value }

#module main

main() {
    x := M.f(value = 10)
    M.f(10) - x.value
}
