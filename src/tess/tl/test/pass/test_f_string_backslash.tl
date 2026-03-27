#module main
#import <Print.tl>

main() {
    x     := 42
    error := 0

    // Backslash escapes in literal segments
    s1 := f"line1\nline2"
    if s1 != "line1\nline2" { error += 1 }

    // Backslash in segment before interpolation hole
    s2 := f"tab\there: {x}"
    if s2 != "tab\there: 42" { error += 1 }

    // Backslash in segment after interpolation hole
    s3 := f"{x}\ttab"
    if s3 != "42\ttab" { error += 1 }

    // Backslash-backslash (literal backslash)
    s4 := f"path: C:\\\\dir\\\\{x}"
    if s4 != "path: C:\\\\dir\\\\42" { error += 1 }

    error
}
