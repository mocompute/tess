#module main

// Closures should work in named helper functions, not just main().
// Currently fails with free_variable_not_found.

helper() {
    x := 10
    f := () { x }
    f() - 10
}

main() {
    helper()
}
