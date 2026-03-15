// Define function in first opening, type with same name in reopened — cross-kind collision.
#module M

f(x: CInt) -> CInt { x }

#module M

f: { v: CInt }

#module main
main() { 0 }
