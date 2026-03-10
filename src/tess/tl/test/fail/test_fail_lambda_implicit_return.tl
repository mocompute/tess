#module main

testfun() {
    () { 42 }
}

main() {
    // Should error, because f() is illegally returning a stack lambda
    r: CInt := testfun()
    r - 42
}
