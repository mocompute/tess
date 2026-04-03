#module main
#import <Print.tl>

main() {
    error := 0

    x := 42

    s1 := f"{x:d}"
    if s1 != "42" { error += 1 }

    s2 := f"{x:8d}"
    if s2 != "      42" { error += 1 }

    s3 := f"{x:08d}"
    if s3 != "00000042" { error += 1 }

    s4 := f"{x:+d}"
    if s4 != "+42" { error += 1 }

    y  := -7
    s5 := f"{y:+d}"
    if s5 != "-7" { error += 1 }

    s6 := f"{y:6d}"
    if s6 != "    -7" { error += 1 }

    u: UInt := 255
    s7      := f"{u:d}"
    if s7 != "255" { error += 1 }

    error
}
