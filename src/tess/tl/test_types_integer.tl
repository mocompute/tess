#module main
#import <stdio.tl>
#import <stdint.tl>

main() {
    error := 0

    error += if 1u == sizeof[CChar]() {0} else {1}
    error += if 1u == sizeof[CUnsignedChar]() {0} else {1}
    error += if 1u == sizeof[CSignedChar]() {0} else {1}

    error += if sizeof[CShort]() == sizeof[CUnsignedShort]() {0} else {1}
    error += if sizeof[CInt]() == sizeof[CUnsignedInt]() {0} else {1}
    error += if sizeof[CLong]() == sizeof[CUnsignedLong]() {0} else {1}

    error += if sizeof[CShort]() >= 2u {0} else {1}
    error += if sizeof[CInt]() >= 2u {0} else {1}
    error += if sizeof[CLong]() >= 4u {0} else {1}
    error += if sizeof[CLongLong]() >= 8u {0} else {1}

    error += if 8u == sizeof[CSize]() {0} else {1}
    error += if 8u == sizeof[CPtrDiff]() {0} else {1}

    // fixed width integers
    error += if 1u == sizeof[CInt8]() {0} else {1}
    error += if 1u == sizeof[CUInt8]() {0} else {1}
    error += if 2u == sizeof[CInt16]() {0} else {1}
    error += if 2u == sizeof[CUInt16]() {0} else {1}
    error += if 4u == sizeof[CInt32]() {0} else {1}
    error += if 4u == sizeof[CUInt32]() {0} else {1}
    error += if 8u == sizeof[CInt64]() {0} else {1}
    error += if 8u == sizeof[CUInt64]() {0} else {1}

    // return error
    error
}
