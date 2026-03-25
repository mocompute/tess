#module main
#import <Print.tl>

main() {
    error := 0

    // Block expression with braces inside interpolation hole
    s1 := f"val = {({a := 10; b := 20; a + b})}"
    if s1 != "val = 30" { error += 1 }

    // Nested block expressions (brace depth > 1)
    s2 := f"val = {({x := ({y := 5; y * 2}); x + 1})}"
    if s2 != "val = 11" { error += 1 }

    // Multiple holes each with braces
    s3 := f"{({1 + 2})} and {({3 + 4})}"
    if s3 != "3 and 7" { error += 1 }

    error
}
