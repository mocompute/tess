#module main

main() {
    x            := 128
    p: Ptr[CInt] := x
    p.*
}
