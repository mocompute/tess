// Test reopening a submodule (not the parent).
#module P

#module P.S
SData: { v: CInt }
create(v: CInt) -> SData { SData(v = v) }

#module P
use_s(v: CInt) -> P.S.SData { P.S.create(v) }

#module P.S
// Reopen submodule: add new function
double(d: SData) -> SData { SData(v = d.v * 2) }

#module main
main() {
    error: CInt := 0

    // Function from first submodule opening
    d := P.S.create(10)
    if d.v != 10 { error += 1 }

    // Function from second submodule opening
    d2 := P.S.double(d)
    if d2.v != 20 { error += 1 }

    // Parent function using submodule
    d3 := P.use_s(5)
    if d3.v != 5 { error += 1 }

    error
}
