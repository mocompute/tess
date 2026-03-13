#module main
#import <Array.tl>

// Test: Cross-module UFCS implicit address-of on Array
// arr.Array.push(x) where push expects Ptr[Array[T]] should implicitly wrap arr in &

main() {
    error := 0

    // Test 1: Basic push via cross-module UFCS
    arr := Array.empty[Int]()
    arr.Array.push(10)
    error += if arr.size == 1zu {0} else {1}

    // Test 2: Multiple pushes
    arr.Array.push(20)
    arr.Array.push(30)
    error += if arr.size == 3zu {0} else {1}

    // Test 3: Verify values were actually pushed
    error += if Array.get(arr, 0zu) == 10 {0} else {1}
    error += if Array.get(arr, 1zu) == 20 {0} else {1}
    error += if Array.get(arr, 2zu) == 30 {0} else {1}

    // Test 4: Mixed explicit and implicit
    Array.push(arr.&, 40)     // explicit addr
    arr.Array.push(50)         // implicit addr
    error += if arr.size == 5zu {0} else {1}
    error += if Array.get(arr, 3zu) == 40 {0} else {1}
    error += if Array.get(arr, 4zu) == 50 {0} else {1}

    // Test 5: pop via cross-module UFCS implicit address-of
    top := arr.Array.pop()
    error += if top == 50 {0} else {1}
    error += if arr.size == 4zu {0} else {1}

    // Test 6: Non-Ptr first param still works — get takes Array[T] (value)
    val := arr.Array.get(0zu)
    error += if val == 10 {0} else {1}

    Array.free(arr.&)

    error
}
