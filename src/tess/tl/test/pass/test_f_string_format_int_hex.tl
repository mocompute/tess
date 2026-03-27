#module main
#import <Print.tl>

main() {
    error := 0

    x := 255

    s1 := f"{x:x}"
    if s1 != "ff" { error += 1 }

    s2 := f"{x:X}"
    if s2 != "FF" { error += 1 }

    s3 := f"{x:#x}"
    if s3 != "0xff" { error += 1 }

    s4 := f"{x:#X}"
    if s4 != "0XFF" { error += 1 }

    error
}
