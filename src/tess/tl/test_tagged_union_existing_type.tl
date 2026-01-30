#module Foo

// A pre-existing struct
Special: { x: Float, y: Float }

// Tagged union that references an existing type as a variant
Shape: | Circle { radius: Float }
       | Foo.Special
       | MyNone

#module main

main() {
    error := 0

    // Construct using unscoped constructor functions (positional args)
    circle := Foo.Circle(3.0)
    none   := Foo.MyNone()

    // Construct the existing type normally
    sp := Foo.Special(x = 1.0, y = 2.0)

    // Wrap the existing type into the tagged union using make_Shape_Special
    shape := Foo.make_Shape_Special(sp)

    // Verify case expression works with the existing type variant
    r1 := case shape: Foo.Shape {
        c: Circle  { c.radius }
        s: Special { s.x + s.y }
        n: MyNone    { 0.0 }
    }
    error += if r1 >= 3.0 {0} else {1}

    // Verify circle constructor still works
    r2 := case circle: Foo.Shape {
        c: Circle  { c.radius }
        s: Special { 0.0 }
        n: MyNone    { 0.0 }
    }
    error += if r2 >= 3.0 {0} else {1}

    error
}
