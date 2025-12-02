#module main
#import <stdio.tl>


main() {
  error := 0

  f : CFloat := 0.0
  error = error + if sizeof(CFloat) < sizeof(CDouble) {0} else {1}

  d : CDouble := f
  error = error + if sizeof(d) == sizeof(CDouble) {0} else {1}

  ld : CLongDouble := f
  error = error + if sizeof(ld) > sizeof(CDouble) {0} else {1}
}
