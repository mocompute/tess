#module main
#import <stdio.tl>
#import <stdint.tl>

main() {
  error := 0

  sz : CSize := c_SIZE_MAX
  error = error + if 8 == sizeof(sz) {0} else {1}

  ui : CUInt8 := sz
  error = error + if 1 == sizeof(ui) {0} else {1}
  error = error + if 255 == ui {0} else {1}

  i : CInt8 := sz
  error = error + if 1 == sizeof(i) {0} else {1}
  error = error + if -1 == i {0} else {1}

  // return error
  error
}
