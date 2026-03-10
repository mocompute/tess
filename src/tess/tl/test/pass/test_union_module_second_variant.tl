#module Foo

Circle: { radius: Float }
Square: { length: Int   }

ShapeTag: { tCircle, tSquare }

ShapeUnion: {
            | circle: Circle
            | square: Square
}

Shape: {
    t: ShapeTag,
    u: ShapeUnion,
}


#module main

main() {
    sq           := Foo.Shape ( t = Foo.ShapeTag.tSquare, u = Foo.ShapeUnion ( square = Foo.Square ( length = 1 ) ) )
    result: CInt := sq.u.square.length - 1
    result
}
