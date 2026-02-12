#module main
#import <stdlib.tl>

Arr(a): {v: Ptr[a], size: Int}

main() {
    ints := Arr(v: Ptr[Int] = c_malloc(sizeof[Int]() * 4), size = 4)
    ints.v.(0) = 1

    floats := Arr(v: Ptr[Float] = c_malloc(sizeof[Float]() * 4), size = 4)
    floats.v.(0) = 3.14

    c_free(ints.v)
    c_free(floats.v)
    0
}
