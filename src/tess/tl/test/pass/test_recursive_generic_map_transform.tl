#module main

// Generic function that transforms a recursive generic type from one
// specialization to another: MyList[T] -> MyList[U]
MyList[T]: | Cons { head: T, tail: Ptr[MyList[T]] }
           | Nil

map_list[T, U](list: MyList[T], f: (T) -> U) -> Ptr[MyList[U]] {
    when list {
        c: Cons {
            next := c.tail.*
            rest := map_list(next, f)
            result: Ptr[MyList[U]] := c_malloc(64zu)
            result.* = Cons(head = f(c.head), tail = rest)
            result
        }
        n: Nil {
            result: Ptr[MyList[U]] := c_malloc(64zu)
            result.* = Nil()
            result
        }
    }
}

sum(list: MyList[Int]) -> Int {
    when list {
        c: Cons { c.head + sum(c.tail.*) }
        n: Nil { 0 }
    }
}

main() {
    nil: MyList[Int] := Nil()
    l3 := Cons(head = 3, tail = nil.&)
    l2 := Cons(head = 2, tail = l3.&)
    l1 := Cons(head = 1, tail = l2.&)

    // Map Int -> Int (double each element)
    double := (x: Int) { x * 2 }
    doubled := map_list(l1, double)

    error := 0
    error += if sum(doubled.*) == 12 { 0 } else { 1 }
    error
}
