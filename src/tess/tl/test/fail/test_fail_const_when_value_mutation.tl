#module main

Shape: | Square { dim: Int }
       | Circle { rad: Int }

main() {
    shape: Const[Shape] := Square(10)
    when shape {
        sq: Square { sq.dim = 5 }
        c: Circle  { c.rad = 5  }
    }
    0
}
