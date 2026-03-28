#module main
#import <cstdlib.tl>

Arr[a]: {data: Ptr[a], size: UInt}

main() {
    ints := Arr(data: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 4u)
    ints.data.[0] = 1

    floats := Arr(data: Ptr[Float] = c_malloc(sizeof[Float]() * 4zu), size = 4u)
    floats.data.[0] = 3.14

    c_free(ints.data)
    c_free(floats.data)
    0
}
