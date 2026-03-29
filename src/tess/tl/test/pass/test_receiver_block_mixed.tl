// Receiver block with forward declarations, definitions, and multiple blocks.

#module Point

Point : { x: Int, y: Int }

// Forward declarations in a receiver block.
p: Ptr[Point] : {
    sum() -> Int
}

// Separate block with definitions.
p: Ptr[Point] : {
    diff() -> Int {
        p->x - p->y
    }
}

// Implementation of the forward declaration.
sum(p) {
    p->x + p->y
}

#module main

main() {
    error := 0
    p := Point(x = 3, y = 7)

    error += if p.sum() == 10 {0} else {1}
    error += if p.diff() == -4 {0} else {1}

    error
}
