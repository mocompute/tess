#module A
Base: {x: Int}

#module B
Alias1 = A.Base

#module C
Alias2 = B.Alias1

#module main

main() {
    error := 0

    // Via type annotation: construct using original type, annotate with chained alias
    v1: C.Alias2 := A.Base(x = 42)
    error += if v1.x == 42 {0} else {1}

    // Direct usage: construct using chained alias
    v2 := C.Alias2(x = 42)
    error += if v2.x == 42 {0} else {1}

    error
}
