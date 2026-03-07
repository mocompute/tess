#module main

// Test basic c_export with default name
[[c_export]] mul(a: CInt, b: CInt) -> CInt { a * b }

// Test c_export with custom name
[[c_export("my_add")]] add(x: CInt, y: CInt) -> CInt { x + y }

// Test c_export with void return
[[c_export]] noop() -> Void { }

// Non-exported function (should not appear in header)
helper(x: CInt) -> CInt { x + 1 }

main() {
    error := 0

    error += if 6 == mul(2, 3) {0} else {1}
    error += if 5 == add(2, 3) {0} else {1}

    error
}
