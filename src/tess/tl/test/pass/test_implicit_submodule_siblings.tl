// Test sibling submodules: both accessible via bare name, no cross-collision.
#module Parent

#module Parent.A
Data:                   { x: CInt     }
create(x: CInt) -> Data { Data(x = x) }

#module Parent.B
Data:                   { y: CInt     }
create(y: CInt) -> Data { Data(y = y) }

#module Parent

// Both A.Data and B.Data accessible without conflict
combine(x: CInt, y: CInt) -> CInt {
    a := A.create(x)
    b := B.create(y)
    a.x + b.y
}

// Synopsis with sibling types
transform(a: A.Data) -> B.Data
transform(a) { B.create(a.x * 2) }

#module main

main() {
    error: CInt := 0

    if Parent.combine(10, 20) != 30 { error += 1 }

    a := Parent.A.create(5)
    b := Parent.transform(a)
    if b.y != 10 { error += 1 }

    error
}
