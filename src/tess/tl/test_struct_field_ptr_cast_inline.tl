#module main
#import <stdlib.tl>

Arr[a]: {
    v:        Ptr[a],
    size:     UInt,
    capacity: UInt,
}

// Forward declaration with two type variables
map[a, b](f: (a) -> b, arr: Arr[a]) -> Arr[b]

// The bug: inlining c_malloc directly into the annotated struct field
// fails, whereas using a temporary variable (out_p) works.
map[a, b](f, arr) {
    // This should be equivalent to:
    //   out_p: Ptr[b] := c_malloc(sizeof(b) * arr.size)
    //   out := Arr(v = out_p, size = arr.size, capacity = arr.size)
    out := Arr(v: Ptr[b] = c_malloc(sizeof[b]() * arr.size), size = arr.size, capacity = arr.size)

    i := 0u
    while i < arr.size {
        out.v.[i] = f(arr.v.[i])
        i         = i + 1u
    }
    out
}

add1(x) { x + 1 }

main() {
    vals := Arr(v: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 0u, capacity = 4u)
    vals.v.[0] = 0
    vals.v.[1] = 1
    vals.v.[2] = 2
    vals.v.[3] = 3
    vals.size  = 4u

    res := map(add1/1, vals)

    sum := 0
    i   := 0u
    while i < res.size, i = i + 1u {
        sum = sum + res.v.[i]
    }
    result : CInt := sum - 10
}
