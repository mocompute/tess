// Test file for TL mode syntax highlighting and indentation
#module TestModule
#import <Array.tl>
#include <stdio.h>

// Test function with various syntax elements
factorial(n: Int) -> Int {
    if n <= 1 {
        1
    } else {
        n * factorial(n - 1)
    }
}

// Test generic function
identity(x: a) -> a {
    x
}

// Test with operators and types
process(arr: Ptr[Array[Int]]) -> Bool {
    size := Array.size(arr)
    i := 0
    while i < size {
        val := Array.get(arr, i)
        Array.set(arr, i, val * 2)
        i += 1
    }
    true
}

// Test struct definition with type constructor
Point[a] : {
    x: a,
    y: a,
}

// Test case expression
classify(n: Int) -> CString {
    case {
        n < 0 -> "negative"
        n == 0 -> "zero"
        n > 0 -> "positive"
    }
}

// Test for loop
sum_array(arr: Array[Int]) -> Int {
    total := 0
    for x <- arr.& {
        total = total + x
    }
    total
}

// Test lambda and closures
make_adder(n: Int) -> (Int -> Int) {
    () {
        x := 0
        (y: Int) -> Int {
            x + y + n
        }
    }
}

// Test C types
c_function(p: Ptr[CInt], size: CSize) -> CDouble {
    result : CDouble := 0.0
    i : CSize := 0
    while i < size {
        result = result + CDouble(p.*.[i])
        i += 1
    }
    result
}

// Test explicit type arguments on functions
push[T](arr: Array[T], val: T) {
    // function with explicit type arg
}

// Test indexing with .[]
test_indexing(arr: Array[Int]) -> Int {
    x := arr.[0]
    arr.[1] = x + 1
    x
}

// Test literals
test_literals() {
    // Numbers
    hex := 0xFF
    binary := 0b1010
    binary_upper := 0B1111_0000
    decimal := 123_456
    float := 3.14159
    scientific := 1.23e-10
    octal := 0777

    // Strings and chars
    str := "Hello, world!\n"
    ch := 'a'
    escaped := '\t'

    // Boolean and null
    flag := true
    ptr := null
}
