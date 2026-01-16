#module Foo

Shape = | Circle { radius: Float }
        | Square { length: Float }

// A different type that is NOT a variant of Shape
Point : { x: Float, y: Float }

#module main

main() {
  s := Foo.Shape_Circle(radius = 2.0)

  // Error: Point is not a valid variant of Shape
  case s: Foo.Shape {
    p: Point { 0.0 }
    else { 1.0 }
  }

  0
}
