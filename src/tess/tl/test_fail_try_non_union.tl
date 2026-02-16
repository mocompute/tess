#module main

// try on a non-union type should fail
test() -> Int {
    x := 42
    y := try x
    y
}

main() { test() }
