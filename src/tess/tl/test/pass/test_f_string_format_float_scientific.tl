#module main
#import <Print.tl>

main() {
    error := 0

    x := 12345.6789

    s1 := f"{x:e}"
    if s1 != "1.234568e+04" { error += 1 }

    s2 := f"{x:.3e}"
    if s2 != "1.235e+04" { error += 1 }

    s3 := f"{x:.2E}"
    if s3 != "1.23E+04" { error += 1 }

    error
}
