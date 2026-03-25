// f-string with no interpolation holes works as a regular string.
// No #import <Print.tl> needed — it degenerates to a plain string literal.

#module main
#import <String.tl>

main() {
    error := 0

    s := f"hello world"
    if s != "hello world" { error += 1 }

    s2 := f""
    if s2 != "" { error += 1 }

    // Escaped braces but no actual interpolation
    s3 := f"{{braces}}"
    if s3 != "{braces}" { error += 1 }

    error
}
