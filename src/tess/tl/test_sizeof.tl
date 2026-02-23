#module main

main() {
    x := 1.0
    // _tl_sizeof_ returns CSize (unsigned); verify it compiles by comparing to itself
    error := 0
    error += if _tl_sizeof_(x) == _tl_sizeof_(x) {0} else {1}
    error
}
