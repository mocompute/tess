#module main

Shape: | Circle { radius: Float }
       | Square { length: Float }

Bogus: { x: Float }

main() {
    s := Circle(1)
    // Error: Bogus is not a variant of Shape
    if b: Bogus := s {
        return 1
    }
    0
}
