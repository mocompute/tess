#module main

// Generic linked list as a recursive tagged union
List[T]: | Cons { head: T, tail: Ptr[List[T]] }
         | Nil

// --- Generic foreach: applies a closure to each element (recursive) ---

foreach[T](list: List[T], f: (T) -> Int) -> Int {
    when list {
        c: Cons {
            f(c.head)
            next := c.tail.*
            foreach(next, f)
        }
        n: Nil { 0 }
    }
}

// --- Sum a list of Ints ---

sum_list(list: List[Int]) -> Int {
    when list {
        c: Cons {
            next := c.tail.*
            c.head + sum_list(next)
        }
        n: Nil { 0 }
    }
}

// --- Length of a list (monomorphic Int) ---

length_list(list: List[Int]) -> Int {
    when list {
        c: Cons {
            next := c.tail.*
            1 + length_list(next)
        }
        n: Nil { 0 }
    }
}

// Test 1: Build a list, use foreach with a closure that captures a mutable accumulator
test_foreach_closure() {
    nil: List[Int] := Nil()
    n3 := Cons(30, nil.&)
    n2 := Cons(20, n3.&)
    n1 := Cons(10, n2.&)

    total := 0
    add_to_total := (x: Int) {
        total = total + x
        0
    }
    foreach(n1, add_to_total)

    if total == 60 {0} else {1}
}

// Test 2: sum_list on a generic list specialized at Int
test_sum_list() {
    nil: List[Int] := Nil()
    n3 := Cons(3, nil.&)
    n2 := Cons(2, n3.&)
    n1 := Cons(1, n2.&)

    result := sum_list(n1)
    // 1 + 2 + 3 = 6
    if result == 6 {0} else {1}
}

// Test 3: Closure that captures a pointer to a recursive generic type
test_closure_captures_list_ptr() {
    nil: List[Int] := Nil()
    n3 := Cons(15, nil.&)
    n2 := Cons(10, n3.&)
    n1 := Cons(5, n2.&)

    // Closure captures n1 (a List[Int]) and calls sum_list on it
    get_sum := () { sum_list(n1) }

    result := get_sum()
    if result == 30 {0} else {1}
}

// Test 4: Length of a list with a closure
test_length_with_closure() {
    nil: List[Int] := Nil()
    n4 := Cons(40, nil.&)
    n3 := Cons(30, n4.&)
    n2 := Cons(20, n3.&)
    n1 := Cons(10, n2.&)

    get_len := () { length_list(n1) }

    if get_len() == 4 {0} else {1}
}

// Test 5: foreach with a closure that multiplies each element
test_foreach_multiply() {
    nil: List[Int] := Nil()
    n3 := Cons(3, nil.&)
    n2 := Cons(2, n3.&)
    n1 := Cons(1, n2.&)

    total := 0
    factor := 10
    add_scaled := (x: Int) {
        total = total + x * factor
        0
    }
    foreach(n1, add_scaled)

    // 10 + 20 + 30 = 60
    if total == 60 {0} else {1}
}

// Test 6: Closure that sums list, called after modifications to captured variables
test_closure_sees_mutations() {
    nil: List[Int] := Nil()
    n3 := Cons(3, nil.&)
    n2 := Cons(2, n3.&)
    n1 := Cons(1, n2.&)

    offset := 0
    compute := () { sum_list(n1) + offset }

    // Before mutation
    r1 := compute()
    offset = 100
    // After mutation -- closure sees updated offset
    r2 := compute()

    error := 0
    error += if r1 == 6  {0} else {1}
    error += if r2 == 106 {0} else {1}
    error
}

// Test 7: Multiple closures capturing the same recursive list
test_multiple_closures_same_list() {
    nil: List[Int] := Nil()
    n3 := Cons(3, nil.&)
    n2 := Cons(2, n3.&)
    n1 := Cons(1, n2.&)

    get_sum    := () { sum_list(n1) }
    get_length := () { length_list(n1) }

    error := 0
    error += if get_sum() == 6    {0} else {1}
    error += if get_length() == 3 {0} else {1}
    error
}

main() {
    error := 0
    error += test_foreach_closure()
    error += test_sum_list()
    error += test_closure_captures_list_ptr()
    error += test_length_with_closure()
    error += test_foreach_multiply()
    error += test_closure_sees_mutations()
    error += test_multiple_closures_same_list()
    error
}
