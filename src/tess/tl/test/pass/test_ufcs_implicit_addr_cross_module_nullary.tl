#module main
#import <Array.tl>

// Cross-module UFCS implicit address-of on nullary mutating functions.

main() {
    error := 0

    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)

    // Implicit module + implicit & on nullary mutating function
    arr.clear()
    error += if arr.size == 0zu {0} else {1}

    arr.push(30)

    // Explicit .Array. still works as alternative
    arr.Array.push(40)
    error += if arr.size == 2zu {0} else {1}

    arr.free()

    error
}
