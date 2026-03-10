// Unknown type name in a nested arrow return type should be rejected.
#module main
make() -> Ptr[Quux] { null }
main() -> CInt      { 0    }
