#module Foo

// tagged union sugar:

Shape = | Circle    { radius: Float }
        | Square    { length: Float }
        | Rectangle { length: Float, height: Float }

// desugars to:

// _ShapeTag_   : { Circle, Square, Rectangle }
// Circle       : { radius: Float }
// Square       : { length: Float }
// Rectangle    : { length: Float, height: Float }
// _ShapeUnion_ : { | Circle: Circle | Square: Square | Rectangle: Rectangle }
// Shape        : { tag: _ShapeTag_, u: _ShapeUnion_ }

#module main

main() {
  0
}
