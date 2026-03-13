#module main
#import <Array.tl>

// Known failure: Cross-module UFCS implicit address-of on nullary mutating functions.
//
// arr.Array.clear() and arr.Array.free() fail at C compilation because the specializer
// treats the function as a closure indirect call instead of a direct function call.
// This happens when the only argument is the implicit-addr-of receiver and there are
// no additional arguments to help drive type specialization.
//
// Workaround: use explicit addr-of — Array.clear(arr.&) or arr.&.Array.clear()

main() {
    error := 0

    arr := Array.empty[Int]()
    arr.Array.push(10)
    arr.Array.push(20)

    arr.Array.clear()
    error += if arr.size == 0zu {0} else {1}

    arr.Array.push(30)
    arr.Array.free()

    error
}
