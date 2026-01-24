#module main

foo() { 42 }

main() {
    fp := foo/0
    pp := fp.&                 // should fail with error on this line - cannot take address of fptr
    0
}
