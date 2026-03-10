#module main

// Circular trait inheritance should be rejected
A[T]: B[T] {
    foo(a: T) -> T
}

B[T]: A[T] {
    bar(a: T) -> T
}

main() { 0 }
