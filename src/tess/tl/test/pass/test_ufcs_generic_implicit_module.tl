#module main
#import <Array.tl>

// Test: UFCS with implicit module inside generic functions

count[T](arr: Array[T]) -> CSize {
    arr.size
}

is_short[T](arr: Array[T]) -> Bool {
    arr.is_empty()
}

first[T](arr: Array[T]) -> T {
    arr.front()
}

main() {
    error := 0

    // Instantiate with Int
    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)

    error += if arr.count() == 2 {0} else {1}
    error += if arr.is_short() == false {0} else {1}
    error += if arr.first() == 10 {0} else {1}

    // Instantiate with Bool
    barr := Array.empty[Bool]()
    barr.push(true)

    error += if barr.count() == 1 {0} else {1}
    error += if barr.is_short() == false {0} else {1}
    error += if barr.first() == true {0} else {1}

    // Empty array
    empty := Array.empty[Int]()
    error += if empty.is_short() == true {0} else {1}

    arr.free()
    barr.free()

    error
}
