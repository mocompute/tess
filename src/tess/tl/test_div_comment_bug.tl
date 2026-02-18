#module main

// Regression: dividing variables whose names generate /*name*/ comments in the
// C output creates an accidental C line comment.
// e.g. `a / b` emits `/*a*/ a_val //*b*/ b_val;` — the `//` is a C comment.

// Direct parameter division
div_params(a: Int, b: Int) -> Int {
    a / b
}

// Division with local variables (also affected)
div_locals(x: Int, y: Int) -> Int {
    a := x
    b := y
    a / b
}

main() {
    error := 0
    error += if div_params(10, 2) == 5 {0} else {1}
    error += if div_params(100, 10) == 10 {0} else {1}
    error += if div_locals(9, 3) == 3 {0} else {1}
    error += if div_locals(20, 4) == 5 {0} else {1}
    error
}
