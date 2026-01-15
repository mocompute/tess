#module Foo

// Tagged union syntax
Shape = | Circle    { radius: Float }
        | Square    { length: Float }

#module main

main() {
  // Test constructor function
  s := Foo.Shape_Circle(radius = 2.0)

  // Test case expression
  result := case s: Foo.Shape {
    c: Circle { c.radius }
    sq: Square { sq.length }
  }

  if result > 1.9 { 0 } else { 1 }
}
