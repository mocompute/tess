#module main
#import <cstdlib.tl>
#import <Unsafe.tl>

Arr[a]: {
    v:        Ptr[a],
    size:     CSize,
    capacity: CSize,
}

// This annotation is not required, but it is helpful for documentation: it reads: "`map` is a function of two arguments.
// The first argument `f` is a generic unary function that maps a value of one type (`a`) to a value of another type (`b`).
// The second argument `arr` is an `Arr` struct with elements of type `a`, passed by value.
// The function returns an `Arr` struct with elements of type `b`.
map[a, b](f: (a) -> b, arr: Arr[a]) -> Arr[b]

// Argument type annotations are not required.
map[a, b](f, arr) {

    // Because of the `c_` prefix, `c_malloc` is compiled to a direct call to the C function `malloc`.
    // Because `malloc` returns `void*`, the `: Ptr[b]` type annotation is required.
    out := Arr(v: Ptr[b] = c_malloc(sizeof[b]() * arr.size), size = arr.size, capacity = arr.size)

    i := 0zu
    while i < arr.size {
        mapped := f(arr.v.[i])
        out.v.[i] = mapped
        i         = i + 1zu
    }
    out
}

add1 (x) { x + 1   }
add1f(x) { x + 1.0 }

main() {

    error := 0

    vals := Arr(v: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 0zu, capacity = 4zu)

    vals.v.[0] = 0
    vals.v.[1] = 1
    vals.v.[2] = 2
    vals.v.[3] = 3
    vals.size  = 4zu

    res := map(add1/1, vals)

    // verify that map does not mutate its argument
    sum := 0
    i   := 0zu
    while i < vals.size, i = i + 1zu {
        sum = sum + vals.v.[i]
    }
    c_assert(sum == 6)
    error += sum - 6

    // verify that map returns an array with mapped values
    sum := 0
    i   := 0zu
    while i < vals.size, i = i + 1zu {
        sum = sum + res.v.[i]
    }
    c_assert(sum == 10)
    error += sum - 10


    floats := Arr(v: Ptr[Float] = c_malloc(sizeof[Float]() * 4zu), size = 0zu, capacity = 4zu)
    floats.v.[0] = 0.0
    floats.v.[1] = 1.0
    floats.v.[2] = 2.0
    floats.v.[3] = 3.0
    floats.size  = 4zu

    floats_res := map(add1f/1, floats)
    sum        := 0.0
    i          := 0zu
    while i < floats.size, i = i + 1zu {
        sum = sum + floats_res.v.[i]
    }

    error += Unsafe.float_to_int(sum - 10.0)
    result: CInt := error
    result
}
