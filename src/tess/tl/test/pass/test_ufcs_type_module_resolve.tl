#module main
#import <Array.tl>

// Test: UFCS resolves module from receiver's type — no explicit .Array. needed
main() {
    error := 0

    arr := Array.empty[Int]()

    // Push via implicit module (Ptr receiver, implicit &)
    arr.push(10)
    arr.push(20)
    arr.push(30)
    arr.push(40)
    error += if arr.size == 4zu {0} else {1}

    // Mixed: explicit and implicit
    arr.Array.push(50)                    // explicit still works
    error += if arr.size == 5zu {0} else {1}

    // Value receiver — element access
    error += if arr.get(0zu) == 10 {0} else {1}
    error += if arr.get(4zu) == 50 {0} else {1}
    error += if arr.front() == 10 {0} else {1}
    error += if arr.back() == 50 {0} else {1}

    // Value receiver — queries
    error += if arr.is_empty() == false {0} else {1}
    error += if arr.contains(30) == true {0} else {1}
    error += if arr.contains(99) == false {0} else {1}

    // Ptr receiver — insert (multiple args)
    arr.insert(0zu, 5)
    error += if arr.get(0zu) == 5 {0} else {1}
    error += if arr.size == 6zu {0} else {1}

    // Ptr receiver — remove
    removed := arr.remove(0zu)
    error += if removed == 5 {0} else {1}
    error += if arr.size == 5zu {0} else {1}

    // Ptr receiver — reverse (nullary mutating)
    arr.reverse()
    error += if arr.front() == 50 {0} else {1}
    error += if arr.back() == 10 {0} else {1}

    // Ptr receiver — pop
    top := arr.pop()
    error += if top == 10 {0} else {1}
    error += if arr.size == 4zu {0} else {1}

    // Ptr receiver — clear (nullary mutating, exercises 604402e2 fix)
    arr.clear()
    error += if arr.size == 0zu {0} else {1}
    error += if arr.is_empty() == true {0} else {1}

    // Cleanup via implicit module
    arr.free()
    error
}
