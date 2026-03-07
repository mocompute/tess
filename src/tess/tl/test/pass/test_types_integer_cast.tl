#module main
#import <stdio.tl>
#import <stdint.tl>
#import <Unsafe.tl>

main() {
    error := 0

    sz: CSize := c_SIZE_MAX
    error += if 8zu == sizeof(sz) {0} else {1}

    ui8: CUInt8 := sz
    error += if 1zu == sizeof(ui8) {0} else {1}
    error += if 0377u == ui8 {0} else {1} // 0 prefix: octal, like C

    i8: CInt8 := sz
    error += if 1zu == sizeof(i8) {0} else {1}
    error += if i8 == -1 {0} else {1}

    ui16: CUInt16 := sz
    error += if 2zu == sizeof(ui16) {0} else {1}
    error += if 0xffffu == ui16 {0} else {1} // 0x prefix: hex, like C

    i16: CInt16 := sz
    error += if 2zu == sizeof(i16) {0} else {1}
    error += if - 1 == i16 {0} else {1}

    error
}
