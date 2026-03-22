#module main
#import <Array.tl>
#import <String.tl>

// Generic wrapper functions in main — UFCS must resolve through these

count[T](arr: Array[T]) -> CSize {
    arr.size
}

first[T](arr: Array[T]) -> T {
    arr.front()
}

// Generic identity — returns whatever it gets, forcing inference through UFCS chain
identity[T](x: T) -> T {
    x
}

main() {
    error := 0

    // Basic UFCS chain: arr.count() returns CSize
    arr := Array.empty[Int]()
    arr.push(42)
    arr.push(99)
    error += if arr.count() == 2zu { 0 } else { 1 }

    // Chain through identity: arr.first().identity() should return 42
    val := arr.first().identity()
    error += if val == 42 { 0 } else { 1 }

    // Chain with Bool array: barr.first().identity() should return true
    barr := Array.empty[Bool]()
    barr.push(true)
    bval := barr.first().identity()
    error += if bval == true { 0 } else { 1 }

    // Chain with String: sarr.first().len() — generic return feeds into module method
    sarr := Array.empty[String]()
    sarr.push("hello")
    slen := sarr.first().len()
    error += if slen == 5zu { 0 } else { 1 }

    // Double chain: sarr.first().identity().len()
    slen2 := sarr.first().identity().len()
    error += if slen2 == 5zu { 0 } else { 1 }

    // Triple chain: identity on identity on first
    val3 := arr.first().identity().identity()
    error += if val3 == 42 { 0 } else { 1 }

    arr.free()
    barr.free()
    sarr.free()

    error
}
