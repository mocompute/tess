// Test that built-in types satisfy compiler-provided trait bounds.
// Built-in types use intrinsics for operators, but should still
// pass trait conformance checks for Eq, Ord, Add, etc.

#module main

are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

is_less[T: Ord](a: T, b: T) -> Bool {
    a < b
}

double[T: Add](x: T) -> T {
    x + x
}

main() {
    error := 0

    // Int satisfies Eq
    error += if are_equal(1, 1) { 0 } else { 1 }
    error += if are_equal(1, 2) { 1 } else { 0 }

    // Int satisfies Ord
    error += if is_less(1, 2) { 0 } else { 1 }
    error += if is_less(2, 1) { 1 } else { 0 }

    // Int satisfies Add
    error += if double(5) == 10 { 0 } else { 1 }

    // Float satisfies Eq, Ord, Add
    error += if are_equal(1.0, 1.0) { 0 } else { 1 }
    error += if is_less(1.0, 2.0) { 0 } else { 1 }
    error += if double(2.5) == 5.0 { 0 } else { 1 }

    // Bool satisfies Eq
    error += if are_equal(1 == 1, 2 == 2) { 0 } else { 1 }

    error
}
