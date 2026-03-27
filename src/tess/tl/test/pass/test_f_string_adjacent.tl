#module main
#import <Print.tl>

main() {
    x     := "hello"
    y     := "world"
    error := 0

    s1 := f"{x}{y}"
    if s1 != "helloworld" { error += 1 }

    s2 := f"{x}{y}!"
    if s2 != "helloworld!" { error += 1 }

    s3 := f"!{x}{y}"
    if s3 != "!helloworld" { error += 1 }

    error
}
