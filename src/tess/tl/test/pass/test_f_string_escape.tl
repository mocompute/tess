#module main
#import <Print.tl>

main() {
    x := 42
    error := 0

    // Escaped braces produce literal { and }
    s1 := f"value: {{{x}}}"
    if s1 != "value: {42}" { error += 1 }

    // Just escaped braces, no interpolation
    s2 := f"no interp: {{hello}}"
    if s2 != "no interp: {hello}" { error += 1 }

    // Escaped open brace adjacent to interpolation
    s3 := f"{{{x}"
    if s3 != "{42" { error += 1 }

    // Escaped close brace after interpolation
    s4 := f"{x}}}"
    if s4 != "42}" { error += 1 }

    error
}
