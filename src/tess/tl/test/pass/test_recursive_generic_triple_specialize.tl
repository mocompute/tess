#module main

// Recursive generic tagged union specialized at three different types
MyList[T]: | Cons { head: T, tail: Ptr[MyList[T]] }
           | Nil

is_nil[T](p: Ptr[MyList[T]]) -> Bool {
    v := p.*
    when v {
        c: Cons { false }
        n: Nil { true }
    }
}

main() {
    a: Ptr[MyList[Int]] := c_malloc(8zu)
    a.* = Nil()

    b: Ptr[MyList[Bool]] := c_malloc(8zu)
    b.* = Nil()

    c: Ptr[MyList[CInt8]] := c_malloc(8zu)
    c.* = Nil()

    error := 0
    error += if is_nil(a) { 0 } else { 1 }
    error += if is_nil(b) { 0 } else { 1 }
    error += if is_nil(c) { 0 } else { 1 }

    c_free(a)
    c_free(b)
    c_free(c)
    error
}
