#module main

// Simple trait with one function
Comparable[T]: {
    compare(a: T, b: T) -> Bool
}

// Trait with multiple functions
Printable[T]: {
    to_string(a: T) -> CString
    print(a: T)     -> CInt
}

// Trait with inheritance (single parent)
Sortable[T]: Comparable[T] {
    rank(a: T, b: T) -> CInt
}

// Combined trait with parents and empty body
Displayable[T]: Comparable[T], Printable[T] { }

main() { 0 }
