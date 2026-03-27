// Tests that generic-only format specs (width/align/fill)
// work with any ToString type, even without ToStringFormat.
#module main
#import <Print.tl>

main() {
    error := 0

    s := "hello"

    s1 := f"{s:>20}"
    if s1 != "               hello" { error += 1 }

    s2 := f"{s:*^20}"
    if s2 != "*******hello********" { error += 1 }

    b := true
    s3 := f"{b:>10}"
    if s3 != "      true" { error += 1 }

    error
}
