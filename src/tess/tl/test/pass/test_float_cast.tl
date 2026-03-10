#module main
#import <stdio.tl>

// Test: float narrowing via let-in annotation, float-integer cast.

main() {
    error := 0

    // Float narrowing: CDouble -> CFloat via let-in annotation
    d:      CDouble := 1.5
    narrow: CFloat  := d
    error += if sizeof(narrow) == sizeof[CFloat]() {0} else {1}

    // Float to integer cast
    f: Float := 3.0
    i: Int   := f
    error += if i == 3 {0} else {1}

    // Integer to float cast
    n:  Int   := 42
    fl: Float := n
    error += if fl == 42.0 {0} else {1}

    // CFloat to CInt cast
    cf: CFloat := 7.0
    ci: CInt   := cf
    error += if ci == 7 {0} else {1}

    error
}
