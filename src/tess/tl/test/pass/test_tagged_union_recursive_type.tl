#module main

// Recursive tagged union: variant with Ptr to the tagged union itself
IntList: | Cons { head: Int, tail: Ptr[IntList] }
         | Nil

main() {
    nil: IntList := Nil()
    n1           := Cons(1, nil.&)
    n2           := Cons(2, n1.&)

    result := when n2 {
        c: Cons { c.head }
        n: Nil { 0 }
    }
    ret: CInt := result - 2
    ret
}
