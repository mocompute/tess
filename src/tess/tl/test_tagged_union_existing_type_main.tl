#module main

// A pre-existing struct in the main module
Special: { x: Float, y: Float }

// Tagged union referencing an existing type from main using main.Special
Shape: | Circle { radius: Float }
       | main.Special
       | MyNone

main() {
    error := 0

    // Construct using unscoped constructor functions
    circle := Circle(3.0)
    none   := MyNone()

    // Construct the existing type normally
    sp := Special(x = 1.0, y = 2.0)

    // Wrap using make_Shape_Special
    shape := make_Shape_Special(sp)

    // Verify when expression works
    r1 := when shape {
        c: Circle  { c.radius }
        s: Special { s.x + s.y }
        n: MyNone    { 0.0 }
    }
    error += if r1 >= 3.0 {0} else {1}

    // Verify circle constructor still works
    r2 := when circle {
        c: Circle  { c.radius }
        s: Special { 0.0 }
        n: MyNone    { 0.0 }
    }
    error += if r2 >= 3.0 {0} else {1}

    error
}
