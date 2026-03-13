#module main
#import <Array.tl>

// Test: UFCS resolves module from receiver's type — no explicit .Array. needed
main() {
    error := 0

    arr := Array.empty[Int]()
    arr.push(10)                          // implicit module from type
    arr.push(20)
    error += if arr.size == 2zu {0} else {1}

    // Mixed: explicit and implicit
    arr.Array.push(30)                    // explicit still works
    arr.push(40)                          // implicit
    error += if arr.size == 4zu {0} else {1}

    // Verify values
    error += if arr.get(0zu) == 10 {0} else {1}
    error += if arr.get(3zu) == 40 {0} else {1}

    // Mutating: pop (expects Ptr receiver, tests implicit addr-of + module resolve)
    top := arr.pop()
    error += if top == 40 {0} else {1}
    error += if arr.size == 3zu {0} else {1}

    Array.free(arr.&)
    error
}
