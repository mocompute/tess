#module main

// Cross-specialization: a function that takes two differently-specialized
// recursive generic types in the same call, forcing both specializations
// to coexist during inference.
MyList[T]: | Cons { head: T, tail: Ptr[MyList[T]] }
           | Nil

head_int(list: MyList[Int]) -> Int {
    when list {
        c: Cons { c.head }
        n: Nil { 0 }
    }
}

head_bool(list: MyList[Bool]) -> Bool {
    when list {
        c: Cons { c.head }
        n: Nil { false }
    }
}

// Takes both specializations simultaneously
check_both(a: MyList[Int], b: MyList[Bool]) -> Int {
    i     := head_int(a)
    bo    := head_bool(b)
    error := 0
    error += if i == 7 { 0 } else { 1 }
    error += if bo == true { 0 } else { 1 }
    error
}

main() {
    nil_i: MyList[Int] := Nil()
    list_i             := Cons(head = 7, tail = nil_i.&)

    nil_b: MyList[Bool] := Nil()
    list_b              := Cons(head = true, tail = nil_b.&)

    result: CInt := check_both(list_i, list_b)
    result
}
