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

    error += if count(arr) == 2zu {0} else {1}
    error += if is_short(arr) == false {0} else {1}
    error += if first(arr) == 10 {0} else {1}

    // Instantiate with Bool
    barr := Array.empty[Bool]()
    barr.push(true)

    error += if count(barr) == 1zu {0} else {1}
    error += if is_short(barr) == false {0} else {1}
    error += if first(barr) == true {0} else {1}

    // Empty array
    empty := Array.empty[Int]()
    error += if is_short(empty) == true {0} else {1}

    arr.free()
    barr.free()

    error
}
