#module main

main() {
    x: CInt := 1
    f       := (n) { x = x + n }
    f(-1)
    x
}
