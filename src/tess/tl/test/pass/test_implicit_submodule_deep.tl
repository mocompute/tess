// Test three-level deep submodule nesting with implicit resolution.
#module A

#module A.B
BData: { x: CInt }
create_b(v: CInt) -> BData { BData(x = v) }

#module A.B.C
CData: { y: CInt }
create_c(v: CInt) -> CData { CData(y = v) }

#module A.B
// In A.B: bare C.func() resolves to A.B.C
use_c(v: CInt) -> C.CData { C.create_c(v) }

#module A
// In A: B.func() resolves to A.B (depth 1)
use_b(v: CInt) -> B.BData { B.create_b(v) }

// In A: B.C.func() resolves to A.B.C (depth 2)
use_bc(v: CInt) -> B.C.CData { B.C.create_c(v) }

#module main
main() {
    error: CInt := 0

    // Depth 1: A -> B
    bd := A.use_b(10)
    if bd.x != 10 { error += 1 }

    // Depth 2: A -> B.C
    cd := A.use_bc(20)
    if cd.y != 20 { error += 1 }

    // B -> C (depth 1 from B's perspective)
    cd2 := A.B.use_c(30)
    if cd2.y != 30 { error += 1 }

    error
}
