#module main
#import <Print.tl>

main() {
    error := 0

    x := 42

    s1 := f"{x:05}"
    if s1 != "00042" { error += 1 }

    s2 := f"{x:08x}"
    if s2 != "0000002a" { error += 1 }

    y  := -42
    s3 := f"{y:08}"
    if s3 != "-0000042" { error += 1 }

    error
}
