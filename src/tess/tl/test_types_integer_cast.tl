#module main
#import <stdio.tl>
#import <stdint.tl>

main() {
  error := 0

  sz : CSize := c_SIZE_MAX
  error = error + if 8 == sizeof(sz) {0} else {1}

  ui8 : CUInt8 := sz
  error = error + if 1 == sizeof(ui8) {0} else {1}
  error = error + if 0377 == ui8 {0} else {1}      // 0 prefix: octal, like C

  i8 : CInt8 := sz
  error = error + if 1 == sizeof(i8) {0} else {1}
  error = error + if -1 == i8 {0} else {1}

  ui16 : CUInt16 := sz
  error = error + if 2 == sizeof(ui16) {0} else {1}
  error = error + if 0xffff == ui16 {0} else {1}  // 0x prefix: hex, like C

  i16 : CInt16 := sz
  error = error + if 2 == sizeof(i16) {0} else {1}
  error = error + if -1 == i16 {0} else {1}

  // return error
  error
}
