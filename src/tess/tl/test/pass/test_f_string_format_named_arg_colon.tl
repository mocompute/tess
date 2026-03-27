// Verifies that colons inside nested braces/parens in an f-string expression
// are NOT misinterpreted as the start of a format specifier.
#module main
#import <Print.tl>

main() {
    error := 0

    // Colon inside a block expression (brace depth > 0)
    s1 := f"val = {({x: Int := 42; x})}"
    if s1 != "val = 42" { error += 1 }

    // Function call inside f-string (no format spec)
    s2 := f"sum = {({a := 10; b := 20; a + b})}"
    if s2 != "sum = 30" { error += 1 }

    error
}
