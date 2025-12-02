#module main
#import <stdio.tl>
#import <stdint.tl>

main() {
  error := 0

  error = error + if 1 == sizeof(CChar) {0} else {1}
  error = error + if 1 == sizeof(CUnsignedChar) {0} else {1}
  error = error + if 1 == sizeof(CSignedChar) {0} else {1}

  error = error + if sizeof(CShort) == sizeof(CUnsignedShort) {0} else {1}
  error = error + if sizeof(CInt) == sizeof(CUnsignedInt) {0} else {1}
  error = error + if sizeof(CLong) == sizeof(CUnsignedLong) {0} else {1}

  error = error + if sizeof(CShort) >= 2 {0} else {1}
  error = error + if sizeof(CInt) >= 2 {0} else {1}
  error = error + if sizeof(CLong) >= 4 {0} else {1}
  error = error + if sizeof(CLongLong) >= 8 {0} else {1}

  error = error + if 8 == sizeof(CSize) {0} else {1}
  error = error + if 8 == sizeof(CPtrDiff) {0} else {1}

  // fixed width integers
  error = error + if 1 == sizeof(CInt8) {0} else {1}
  error = error + if 1 == sizeof(CUInt8) {0} else {1}
  error = error + if 2 == sizeof(CInt16) {0} else {1}
  error = error + if 2 == sizeof(CUInt16) {0} else {1}
  error = error + if 4 == sizeof(CInt32) {0} else {1}
  error = error + if 4 == sizeof(CUInt32) {0} else {1}
  error = error + if 8 == sizeof(CInt64) {0} else {1}
  error = error + if 8 == sizeof(CUInt64) {0} else {1}

  // return error
  error
}
