#module Foo


Circle : { radius : Float }
Square : { length : Int }

ShapeTag : { tCircle, tSquare }
ShapeUnion : {
  | circle : Circle
  | square : Square
}

Shape : {
  t : ShapeTag,
  u : ShapeUnion,
}


#module main

main() {
  c := Foo.Circle { radius = 0.5 }
  u := Foo.ShapeUnion { circle = c }
  t := Foo.ShapeTag.tCircle
  circ := Foo.Shape { t = t, u = u }
  // sq := Foo.Shape { t = Foo.ShapeTag.tCircle, u = Foo.ShapeUnion { circle = Foo.Circle { radius = 0.5 } } }
  0
}
