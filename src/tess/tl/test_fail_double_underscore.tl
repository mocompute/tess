#module main

// Error: identifiers containing __ are reserved for compiler name mangling
my__var := 42

main() {
  my__var
}
