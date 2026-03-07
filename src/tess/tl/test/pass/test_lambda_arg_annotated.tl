#module main

// The bug happens only if `f` has a type annotation. Without it, the bug does not happen.
apply(f: (Int) -> Void, y) -> Void {
    f(y)
}


main() {
    error := 0
    count := 0

    apply((x) { count = count + x }, 3)

    if count != 3 { error += 1 }
    error
}
