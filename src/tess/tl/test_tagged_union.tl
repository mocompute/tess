#module Foo

// Tagged union syntax - this should desugar to the types below
Shape = | Circle    { radius: Float }
        | Square    { length: Float }
        | Rectangle { length: Float, height: Float }

// The above desugars to:
// _ShapeTag_   : { Circle, Square, Rectangle }
// Circle       : { radius: Float }
// Square       : { length: Float }
// Rectangle    : { length: Float, height: Float }
// _ShapeUnion_ : { | Circle: Circle | Square: Square | Rectangle: Rectangle }
// Shape        : { tag: _ShapeTag_, u: _ShapeUnion_ }

#module main

main() {
  // Test construction using the desugared types
  c := Foo.Circle(radius = 1.0)
  u := Foo._ShapeUnion_(Circle = c)
  s := Foo.Shape(tag = Foo._ShapeTag_.Circle, u = u)

  // Test access
  if s.tag == Foo._ShapeTag_.Circle {0} else {1}
}
