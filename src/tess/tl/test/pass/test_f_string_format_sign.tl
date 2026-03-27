#module main
#import <Print.tl>

main() {
    error := 0

    x := 42
    y := -42

    s1 := f"{x:+}"
    if s1 != "+42" { error += 1 }

    s2 := f"{y:+}"
    if s2 != "-42" { error += 1 }

    s3 := f"{x: }"
    if s3 != " 42" { error += 1 }

    s4 := f"{y: }"
    if s4 != "-42" { error += 1 }

    error
}
