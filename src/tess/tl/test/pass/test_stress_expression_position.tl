#module main

// Block expression as struct field initializer
Point: { x: Int, y: Int }

test_block_as_field_init() {
    p := Point(
               x = ( { 1 + 2 }),
               y = ( { 3 + 4 })
               )
    error := 0
    error += if p.x == 3 {0} else {1}
    error += if p.y == 7 {0} else {1}
    error
}

// if expression as function argument
add(a: Int, b: Int) -> Int { a + b }

test_if_as_argument() {
    result := add(
                  if 1 == 1 { 10 } else { 0 },
                  if 2 == 2 { 20 } else { 0 }
                  )
    if result == 30 {0} else {1}
}

// Nested if as both branches of outer if
test_nested_if_both_branches() {
    a := 1
    b := 2

    result := if a == 1 {
        if b == 2 { 100 } else { 50 }
    } else {
        if b == 2 { 25 } else { 0 }
    }
    if result == 100 {0} else {1}
}

// Block expression as addition operand
test_block_in_arithmetic() {
    result := ( {
               a := 10
               a
               }) + ( {
               b := 20
               b
               }) + ( {
               c := 30
               c
               })
    if result == 60 {0} else {1}
}

// Chained let-in as while loop bound
test_let_in_as_loop_bound() {
    limit := (x := 3
              y := x + 2
              y)
    sum := 0
    i   := 0
    while i < limit, i = i + 1 {
        sum = sum + i
    }
    // 0 + 1 + 2 + 3 + 4 = 10
    if sum == 10 {0} else {1}
}

// if expression inside a block expression inside arithmetic
test_deeply_nested_expression() {
    result := 1 + ( {
                   x := if 1 == 1 { 10 } else { 0 }
                   y := if 2 == 2 { 20 } else { 0 }
                   x + y
                   })
    if result == 31 {0} else {1}
}

Shape: | Circle { radius: Int }
       | Square { length: Int }

// when as case scrutinee value
test_when_then_case() {
    s   := Circle(2)
    val := when s {
        c:  Circle { c.radius }
        sq: Square { sq.length }
    }
    result := case val {
        1 { 10 }
        2 { 20 }
        3 { 30 }
        else { 0 }
    }
    if result == 20 {0} else {1}
}

// Block expression as pointer index
test_block_as_index() {
    arr: CArray[Int, 5] := void
    ptr: Ptr[Int]       := arr
    ptr.[0] = 10
    ptr.[1] = 20
    ptr.[2] = 30
    ptr.[3] = 40
    ptr.[4] = 50

    idx    := ( { 1 + 1 })
    result := ptr.[idx]
    if result == 30 {0} else {1}
}

main() {
    error := 0
    error += test_block_as_field_init()
    error += test_if_as_argument()
    error += test_nested_if_both_branches()
    error += test_block_in_arithmetic()
    error += test_let_in_as_loop_bound()
    error += test_deeply_nested_expression()
    error += test_when_then_case()
    error += test_block_as_index()
    error
}
