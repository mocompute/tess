// Unknown type name in a function parameter should be rejected.
#module main
identity(x: Quux) -> Int  { x            }
main    ()        -> CInt { identity(42) }
