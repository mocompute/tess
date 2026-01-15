#module Foo

// Tagged union syntax
Shape = | Circle    { radius: Float }
        | Square    { length: Float }
        | None

#module main

main() {
  // Test constructor function
  s      := Foo.Shape_Circle(radius = 2.0)
  square := Foo.Shape_Square(length = 1.5) // unused
  none   := Foo.Shape_None                 // unused
  none2  := Foo.Shape_None()               // also works

  // Test case expression. There is no exhaustiveness checking.
  // FIXME: without an else case, area would be left undefined.
  area := case s: Foo.Shape {
    c:  Circle { c.radius * c.radius * 3.14159 }
    sq: Square { sq.length * sq.length }
    else       { 0.0 }                     // optional else case
  }

  if area > 12.0 { 0 } else { 1 }
}
