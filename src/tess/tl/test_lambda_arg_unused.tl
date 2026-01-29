#module main

apply(f, y) -> Void {
    f(y)
}


main() {
    error := 0
    count := 0

    // The bug happens only if `x` does not appear in the body of the lambda.
    apply((x) { count = count + 3 }, 0)

    // If instead this line were used, the test would work:
    // apply((x) { count = count + x }, 3)

    if count != 3 { error += 1 }
    error
}
