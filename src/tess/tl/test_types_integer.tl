#module main
#import <stdio.tl>
#import <stdint.tl>

main() {
    error := 0

    error += if 1zu == sizeof[CChar]() {0} else {1}
    error += if 1zu == sizeof[CUnsignedChar]() {0} else {1}
    error += if 1zu == sizeof[CSignedChar]() {0} else {1}

    error += if sizeof[CShort]() == sizeof[CUnsignedShort]() {0} else {1}
    error += if sizeof[CInt]() == sizeof[CUnsignedInt]() {0} else {1}
    error += if sizeof[CLong]() == sizeof[CUnsignedLong]() {0} else {1}

    error += if sizeof[CShort]() >= 2zu {0} else {1}
    error += if sizeof[CInt]() >= 2zu {0} else {1}
    error += if sizeof[CLong]() >= 4zu {0} else {1}
    error += if sizeof[CLongLong]() >= 8zu {0} else {1}

    error += if 8zu == sizeof[CSize]() {0} else {1}
    error += if 8zu == sizeof[CPtrDiff]() {0} else {1}

    // fixed width integers
    error += if 1zu == sizeof[CInt8]() {0} else {1}
    error += if 1zu == sizeof[CUInt8]() {0} else {1}
    error += if 2zu == sizeof[CInt16]() {0} else {1}
    error += if 2zu == sizeof[CUInt16]() {0} else {1}
    error += if 4zu == sizeof[CInt32]() {0} else {1}
    error += if 4zu == sizeof[CUInt32]() {0} else {1}
    error += if 8zu == sizeof[CInt64]() {0} else {1}
    error += if 8zu == sizeof[CUInt64]() {0} else {1}

    // return error
    error
}
