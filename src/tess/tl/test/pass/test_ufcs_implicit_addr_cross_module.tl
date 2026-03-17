#module main
#import <Array.tl>

// Test: Cross-module UFCS implicit address-of on Array
// arr.push(x) where push expects Ptr[Array[T]] should implicitly wrap arr in &

main() {
    error := 0

    // Test 1: Basic push via implicit module UFCS
    arr := Array.empty[Int]()
    arr.push(10)
    error += if arr.size == 1zu {0} else {1}

    // Test 2: Multiple pushes
    arr.push(20)
    arr.push(30)
    error += if arr.size == 3zu {0} else {1}

    // Test 3: Verify values were actually pushed
    error += if arr.get(0zu) == 10 {0} else {1}
    error += if arr.get(1zu) == 20 {0} else {1}
    error += if arr.get(2zu) == 30 {0} else {1}

    // Test 4: Mixed explicit and implicit
    Array.push(arr.&, 40)               // fully explicit
    arr.push(50)                        // implicit module + implicit addr
    arr.Array.push(60)                  // explicit module, implicit addr (alternative style)
    error += if arr.size == 6zu {0} else {1}
    error += if arr.get(3zu) == 40 {0} else {1}
    error += if arr.get(4zu) == 50 {0} else {1}
    error += if arr.get(5zu) == 60 {0} else {1}

    // Test 5: pop via implicit module UFCS
    top := arr.pop()
    error += if top == 60 {0} else {1}
    error += if arr.size == 5zu {0} else {1}

    // Test 6: Non-Ptr first param still works — get takes Array[T] (value)
    val := arr.get(0zu)
    error += if val == 10 {0} else {1}

    arr.free()

    error
}
