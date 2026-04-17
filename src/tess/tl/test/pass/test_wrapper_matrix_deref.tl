// Wrapper-matrix coverage for pointer dereference (*p) and index (p[i]).
// Each op's result type must preserve inner Const when the pointer is
// Ptr[Const[T]], so the result cannot be written through.

#module main

#import <cstdlib.tl>

read_ptr      (p: Ptr[Int])        -> Int { p.* }
read_ptr_const(p: Ptr[Const[Int]]) -> Int { p.* }

index_ptr      (p: Ptr[Int], i: CSize)        -> Int { p.[i] }
index_ptr_const(p: Ptr[Const[Int]], i: CSize) -> Int { p.[i] }

main() {
    // Ptr[T]
    pi: Ptr[Int] := c_malloc(sizeof[Int]() * 4)
    defer c_free(pi)
    pi.[0] = 10
    pi.[1] = 20
    pi.[2] = 30
    pi.[3] = 40

    error: CInt := 0

    // *p on Ptr[T]
    if read_ptr(pi) != 10 { error += 1 }

    // *p on Ptr[Const[T]] (implicit coercion)
    if read_ptr_const(pi) != 10 { error += 1 }

    // p[i] on Ptr[T]
    if index_ptr(pi, 2) != 30 { error += 1 }

    // p[i] on Ptr[Const[T]]
    if index_ptr_const(pi, 3) != 40 { error += 1 }

    // Const[Ptr[T]]: const binding of a pointer still allows deref + index.
    cpi: Const[Ptr[Int]] := pi
    if cpi.* != 10 { error += 1 }
    if cpi.[1] != 20 { error += 1 }

    error
}
