#module main
#import <Print.tl>

main() {
    error := 0

    x := 42

    s1 := f"{x:*>10}"
    if s1 != "********42" { error += 1 }

    s2 := f"{x:.<10}"
    if s2 != "42........" { error += 1 }

    s3 := f"{x:-^10}"
    if s3 != "----42----" { error += 1 }

    error
}
