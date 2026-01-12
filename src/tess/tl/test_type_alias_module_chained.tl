#module A
Base : {x: Int}

#module B
Alias1 = A.Base

#module C
Alias2 = B.Alias1

#module main
main() {
    v : C.Alias2 := A.Base(x = 42)
    if (v.x == 42) { 0 } else { 1 }
}
