#import "builtin.tl"

#module A
Base : {x: Int}

#module B
Alias1 = A.Base

#module C
Alias2 = B.Alias1

#module main
main() {
    // Direct usage: construct using chained alias
    v := C.Alias2(x = 42)
    if (v.x == 42) { 0 } else { 1 }
}
