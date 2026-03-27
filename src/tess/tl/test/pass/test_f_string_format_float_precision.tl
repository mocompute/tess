#module main
#import <Print.tl>

main() {
    error := 0

    pi := 3.14159265

    s1 := f"{pi:.2f}"
    if s1 != "3.14" { error += 1 }

    s2 := f"{pi:.0f}"
    if s2 != "3" { error += 1 }

    s3 := f"{pi:.4f}"
    if s3 != "3.1416" { error += 1 }

    x := 1.0
    s4 := f"{x:.2f}"
    if s4 != "1.00" { error += 1 }

    error
}
