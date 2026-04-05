// The loop variable `i` must not be visible after the for loop.
// This test should fail to compile.
#module main

main() -> CInt {
    for i := 0, i < 5, i += 1 {
        _ := i
    }
    _ := i                              // error: i is out of scope
    0
}
