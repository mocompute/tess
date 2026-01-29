#module main

// Test: Generic struct where the generic type IS the function pointer type
// Extended tests with single-argument and two-argument functions

double (x: Int) -> Int { x * 2 }
triple (x: Int) -> Int { x * 3 }
add_one(x: Int) -> Int { x + 1 }

// Generic struct where T can be any type including function pointers
Wrapper(T): { callback: T, id: Int }

main() {
    error := 0

    // Test 1: Wrapper infers function pointer type from double/1
    w1      := Wrapper(callback = double/1, id = 1)
    result1 := w1.callback(5)
    error += if result1 == 10 {0} else {1}

    // Test 2: Wrapper infers function pointer type from triple/1
    w2      := Wrapper(callback = triple/1, id = 2)
    result2 := w2.callback(5)
    error += if result2 == 15 {0} else {1}

    // Test 3: Direct construction with add_one
    w3      := Wrapper(callback = add_one/1, id = 3)
    result3 := w3.callback(10)
    error += if result3 == 11 {0} else {1}

    // Test 4: Check id field is preserved
    error += if w1.id == 1 {0} else {1}
    error += if w2.id == 2 {0} else {1}
    error += if w3.id == 3 {0} else {1}

    // Test 5: Reassign callback and invoke again
    cb      := w1.callback
    result5 := cb(7)
    error += if result5 == 14 {0} else {1}

    error
}
