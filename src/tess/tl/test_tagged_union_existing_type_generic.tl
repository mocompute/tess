#module Foo

// A pre-existing generic struct
Pair[a]: { x: a, y: a }

// Tagged union that references an existing generic type as a variant
Shape[T]: | Circle { radius: T }
          | Foo.Pair[T]
          | MyNone

#module main

main() {
    error := 0

    // Construct existing generic type normally
    p := Foo.Pair(x = 1, y = 2)

    // Wrap the existing type into the tagged union using make function
    shape := Foo.make_Shape_Pair(p)

    // Verify case expression works
    r1 := case shape: Foo.Shape {
        c: Circle { c.radius }
        p: Pair { p.x + p.y }
        n: MyNone { 0 }
    }
    error += if r1 == 3 {0} else {1}

    // Verify circle still works with the generic type param
    circle := Foo.Circle(3.14)
    r2     := case circle: Foo.Shape {
        c: Circle { c.radius }
        p: Pair { 0.0 }
        n: MyNone { 0.0 }
    }
    error += if r2 >= 3.0 {0} else {1}

    error
}
