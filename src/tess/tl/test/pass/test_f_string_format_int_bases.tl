#module main
#import <Print.tl>

main() {
    error := 0

    x := 42

    s1 := f"{x:o}"
    if s1 != "52" { error += 1 }

    s2 := f"{x:#o}"
    if s2 != "0o52" { error += 1 }

    s3 := f"{x:b}"
    if s3 != "101010" { error += 1 }

    s4 := f"{x:#b}"
    if s4 != "0b101010" { error += 1 }

    error
}
