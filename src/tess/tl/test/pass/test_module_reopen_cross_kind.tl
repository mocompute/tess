// Function and type with the same user-visible name in a module.
// Functions are arity-mangled, so they occupy a separate namespace from types.
#module M

f: { v: CInt }

#module M

f(x: CInt) -> CInt { s := f(v = x); s.v }

#module main

main() {
    x := M.f(v = 10)
    M.f(10) - x.v
}
