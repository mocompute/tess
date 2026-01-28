#module A

Shape : | Circle { radius: Float }
        | Square { length: Float }

#module B

Shape : { x: Int, y: Int }

#module main

main() {
  // B.Shape has no nested type Circle, but A.Shape does.
  // This should fail, not silently mangle to B_Shape_Circle.
  bad := B.Shape.Circle(radius = 1.0)
  0
}
