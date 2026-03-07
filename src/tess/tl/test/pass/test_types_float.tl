#module main
#import <stdio.tl>


main() {
    error := 0

    f: CFloat := 0.0
    error += if sizeof[CFloat]() < sizeof[CDouble]() {0} else {1}

    d: CDouble := f
    error += if sizeof(d) == sizeof[CDouble]() {0} else {1}

    // On macOS, sizeof long double == sizeof double
    ld: CLongDouble := f
    error += if sizeof(ld) >= sizeof[CDouble]() {0} else {1}

    c_printf(c"sizeof CFloat = %u\n", sizeof[CFloat]())
    c_printf(c"sizeof CDouble = %u\n", sizeof[CDouble]())
    c_printf(c"sizeof CLongDouble = %u\n", sizeof[CLongDouble]())

    error

}
