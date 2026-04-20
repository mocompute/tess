#module main

Point[T]: { x: T, y: T }

test_struct() {

    error := 0

    sz := _tl_sizeof_[Point[Int]]()

    if sz != 2 * _tl_sizeof_[Int]() { error += 1 }

    error
}

main() {
    x := 1.0
    // _tl_sizeof_ returns CSize (unsigned); verify it compiles by comparing to itself
    error := 0
    error += if _tl_sizeof_(x) == _tl_sizeof_(x) {0} else {1}
    error += test_struct()
    error
}
