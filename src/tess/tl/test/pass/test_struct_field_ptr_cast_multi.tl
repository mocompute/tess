#module main
#import <stdlib.tl>

Arr[a]: {v: Ptr[a], size: UInt}

main() {
    ints := Arr(v: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 4u)
    ints.v.[0] = 1

    floats := Arr(v: Ptr[Float] = c_malloc(sizeof[Float]() * 4zu), size = 4u)
    floats.v.[0] = 3.14

    c_free(ints.v)
    c_free(floats.v)
    0
}
