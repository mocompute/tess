#module main
#import <Print.tl>

main() {
    error := 0

    x  := 42
    s1 := f"{x:10}"
    if s1 != "        42" { error += 1 }

    s  := "hi"
    s2 := f"{s:10}"
    if s2 != "        hi" { error += 1 }

    error
}
