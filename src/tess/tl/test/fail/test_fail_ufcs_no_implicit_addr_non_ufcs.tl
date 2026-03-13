#module main
#import <Array.tl>

// Test: implicit address-of must NOT apply to regular (non-UFCS) calls.
// Array.push(arr, 1) should fail because arr is Array[Int], not Ptr[Array[Int]].

main() {
    arr := Array.empty[Int]()
    Array.push(arr, 1)
}
