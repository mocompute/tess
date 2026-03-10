// Unknown type name in a return type should be rejected.
#module main
make() -> Quux { 42     }
main() -> CInt { make() }
