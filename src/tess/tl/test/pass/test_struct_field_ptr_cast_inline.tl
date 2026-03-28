#module main
#import <cstdlib.tl>

Arr[a]: {
    data:     Ptr[a],
    size:     CSize,
    capacity: CSize,
}

// Forward declaration with two type variables
map[a, b](f: (a) -> b, arr: Arr[a]) -> Arr[b]

// The bug: inlining c_malloc directly into the annotated struct field
// fails, whereas using a temporary variable (out_p) works.
map[a, b](f, arr) {
    // This should be equivalent to:
    //   out_p: Ptr[b] := c_malloc(sizeof(b) * arr.size)
    //   out := Arr(v = out_p, size = arr.size, capacity = arr.size)
    out := Arr(data: Ptr[b] = c_malloc(sizeof[b]() * arr.size), size = arr.size, capacity = arr.size)

    i := 0zu
    while i < arr.size {
        out.data.[i] = f(arr.data.[i])
        i            = i + 1zu
    }
    out
}

add1(x) { x + 1 }

main() {
    vals := Arr(data: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = 0zu, capacity = 4zu)
    vals.data.[0] = 0
    vals.data.[1] = 1
    vals.data.[2] = 2
    vals.data.[3] = 3
    vals.size     = 4zu

    res := map(add1/1, vals)

    sum := 0
    i   := 0zu
    while i < res.size, i = i + 1zu {
        sum = sum + res.data.[i]
    }
    result: CInt := sum - 10
    result
}
