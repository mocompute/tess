#module Test

f() {
    return () { 42 }
    0
}

#module main

main() {
    f()
}
