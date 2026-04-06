#module main

// Three-variant union — discard detection works for all tagged unions
Shape: | Circle { radius: Float }
       | Square { length: Float }
       | Triangle

make_shape() -> Shape {
    Circle(1.0)
}

test_three_variant() -> CInt {
    make_shape()
    0
}

main() {
    test_three_variant()
    0
}
