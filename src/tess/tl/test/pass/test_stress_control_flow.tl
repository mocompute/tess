#module MyOption

T[a]: | MySome { v: a }
      | MyNone

#module main

Shape: | Circle { radius: Int }
       | Square { length: Int }
       | MyNone

// break from a when arm inside a while loop
test_break_from_when() {
    shapes: CArray[Shape, 4] := void
    shapes.[0] = Circle(1)
    shapes.[1] = Square(2)
    shapes.[2] = Circle(10)
    shapes.[3] = Square(4)

    found           := 0
    i               := 0
    ptr: Ptr[Shape] := shapes
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        when elem {
            c: Circle {
                if c.radius >= 10 {
                    found = c.radius
                    break
                }
            }
            else { }
        }
    }
    if found == 10 {0} else {1}
}

// continue from a when inside a while loop
test_continue_from_when() {
    shapes: CArray[Shape, 4] := void
    shapes.[0] = Circle(1)
    shapes.[1] = MyNone()
    shapes.[2] = Circle(3)
    shapes.[3] = MyNone()

    sum             := 0
    i               := 0
    ptr: Ptr[Shape] := shapes
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        when elem {
            n: MyNone { continue }
            else { }
        }
        sum = sum + when elem {
            c:  Circle { c.radius }
            sq: Square { sq.length }
            else { 0 }
        }
    }
    // 1 + 3 = 4
    if sum == 4 {0} else {1}
}

// return from deep inside nested when/if chains
deep_return(s: Shape, flag: Int) -> Int {
    when s {
        c: Circle {
            if flag > 0 {
                if c.radius > 5 {
                    return 999
                }
                c.radius
            } else {
                0
            }
        }
        sq: Square { sq.length }
        n:  MyNone { -1 }
    }
}

test_deep_return() {
    error := 0
    // Should trigger deep return
    error += if deep_return(Circle(10), 1) == 999 {0} else {1}
    // Should not trigger return
    error += if deep_return(Circle(3), 1) == 3 {0} else {1}
    // flag <= 0, returns 0
    error += if deep_return(Circle(10), 0) == 0 {0} else {1}
    error
}

// Tail call from a when arm
count_down(s: Shape) -> Int {
    when s {
        c: Circle {
            if c.radius <= 0 { 0 }
            else { count_down(Circle(c.radius - 1)) }
        }
        else { -1 }
    }
}

test_tail_call_from_when() {
    result := count_down(Circle(100))
    if result == 0 {0} else {1}
}

// Bail (unwrap-or-bail) inside a loop with break
test_bail_in_loop_break() {
    result := 0
    i      := 0
    while i < 5, i = i + 1 {
        val       := if i == 3 { MyOption.MySome(42) } else { MyOption.MyNone() }
        s: MySome := val else { continue }
        result = s.v
        break
    }
    if result == 42 {0} else {1}
}

// Bail (unwrap-or-bail) with return
test_bail_with_return() -> Int {
    val       := MyOption.MySome(77)
    s: MySome := val else { return 1 }
    if s.v == 77 {0} else {1}
}

main() {
    error := 0
    error += test_break_from_when()
    error += test_continue_from_when()
    error += test_deep_return()
    error += test_tail_call_from_when()
    error += test_bail_in_loop_break()
    error += test_bail_with_return()
    result: CInt := error
    result
}
