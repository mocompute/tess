// Unknown type name in an arrow parameter type annotation should be rejected.
#module main
apply(f: (Quux) -> CInt) -> CInt { f(null) }
main ()                  -> CInt { 0       }
