#module main

Shape: | Circle { radius: Float }
       | Square { length: Float }
       | MyNone

main() {
    error := 0

    // Store tagged unions in a CArray
    shapes: CArray[Shape, 4] := void
    shapes.[0] = Circle(1.0)
    shapes.[1] = Square(2.0)
    shapes.[2] = MyNone()
    shapes.[3] = Circle(3.0)

    // Match each element via intermediate variables
    s0 := shapes.[0]
    r0 := when s0 {
        c: Circle { c.radius }
        else { 0.0 }
    }
    error += if r0 == 1.0 {0} else {1}

    s1 := shapes.[1]
    r1 := when s1 {
        s: Square { s.length }
        else { 0.0 }
    }
    error += if r1 == 2.0 {0} else {1}

    s2 := shapes.[2]
    r2 := when s2 {
        n: MyNone { -1.0 }
        else { 0.0 }
    }
    error += if r2 == -1.0 {0} else {1}

    // Sum all radii using a loop over decayed pointer
    sum             := 0.0
    ptr: Ptr[Shape] := shapes
    i               := 0
    while i < 4, i = i + 1 {
        elem := ptr.[i]
        sum = sum + when elem {
            c: Circle { c.radius }
            else { 0.0 }
        }
    }
    error += if sum == 4.0 {0} else {1}

    error
}
