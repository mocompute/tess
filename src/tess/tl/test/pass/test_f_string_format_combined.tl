#module main
#import <Print.tl>

main() {
    error := 0

    pi := 3.14159265

    s1 := f"{pi:>+10.2f}"
    if s1 != "     +3.14" { error += 1 }

    x  := 255
    s2 := f"{x:#010x}"
    if s2 != "0x000000ff" { error += 1 }

    s3 := f"pi is {pi:.2f}, hex is {x:#x}"
    if s3 != "pi is 3.14, hex is 0xff" { error += 1 }

    error
}
