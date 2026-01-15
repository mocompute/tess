#module Foo

// Tagged union syntax
Shape = | Circle    { radius: Float }
        | Square    { length: Float }

#module main

main() {
  // Test constructor function
  s      := Foo.Shape_Circle(radius = 2.0)
  square := Foo.Shape_Square(length = 1.5) // unused

  // Test case expression
  area := case s: Foo.Shape {
    c:  Circle { c.radius * c.radius * 3.14159 }
    sq: Square { sq.length * sq.length }
  }

  if area > 12.0 { 0 } else { 1 }
}
