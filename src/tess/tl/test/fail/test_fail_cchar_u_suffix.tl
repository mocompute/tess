// u-suffixed literal should not bind to CChar
#module main

main() {
    x: CChar := 65u
    x
}
