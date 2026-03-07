// Unsuffixed integer literals are family-agnostic and can unify with CChar.
#module main

take_cchar(x: CChar) -> CChar { x }

main() {
    error := 0
    error += if take_cchar(65) == 'A' { 0 } else { 1 }
    error
}
