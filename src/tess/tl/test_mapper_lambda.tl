#module main
#import <stdlib.tl>


// A generic type definition
Arr[T]: {
    v:        Ptr[T],
    size:     UInt,
    capacity: UInt,
}

// Annotation not required, but can be good for documentation: f is a function pointer (noted by the
// presence of the arrow `->` operator), which takes an arbitrary type `T` and returns an arbitrary type `U`
// (which may or may not differ from `T`).

map[U](f, arr) {
    // out_p must be annotated as a `Ptr[U]}. U is declared as an explicit type parameter above.
    out_p: Ptr[U] := c_malloc(sizeof[U]() * arr.size)

    // Construct an Arr object on the stack
    out := Arr(v = out_p, size = arr.size, capacity = arr.size)

    // Iteration is with `while`, with an optional `, expr` update expression (note the comma) which activates
    // at the end of the loop. `continue` is supported with the same semantics as C. Collections can also
    // provide custom iteration functions which can be access with a `for` statement.
    i := 0u
    while i < arr.size, i = i + 1u {
        out.v.[i] = f(arr.v.[i])
    }

    // the value of the final expression is the return value. In this case, the struct will be copied to the
    // caller's stack frame (potentially optimized).
    out
}

main() {

    // When constructing an object, all fields must be provided. But they may be assigned `void`, which leaves
    // them uninitialised, shown here for the purpose of an example.
    vals := Arr(v: Ptr[Int] = c_malloc(sizeof[Int]() * 4zu), size = void, capacity = 4u)

    // Note that `:=` always defines a new variable. A plain `=` is used to assign to an existing variable (or
    // a field or indexed location).
    vals.size  = 4u
    vals.v.[0] = 0
    vals.v.[1] = 1
    vals.v.[2] = 2
    vals.v.[3] = 3

    // A lambda function is parsed as `(...) { ... }`, that is, a set of 0 or more parameters and a body.
    res := map( (x) { x + 1 }, vals )

    // Verify that map does not mutate its argument. Note that statements need not be separated by a newline,
    // any whitespace will do, or optional commas may be used.
    sum := 0, i := 0u
    while i < vals.size, i = i + 1u {
        sum = sum + vals.v.[i]
    }
    c_assert(sum == 6)

    // verify that map returns an array with mapped values
    sum := 0, i := 0u
    while i < vals.size, i = i + 1u {
        sum = sum + res.v.[i]
    }
    c_assert(sum == 10)

    result: CInt := sum - 10
    // result
    0

    // The two c_malloc'd buffers are leaked here.
}
