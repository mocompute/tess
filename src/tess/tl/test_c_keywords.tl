#module main

// Test that C reserved keywords can be used as Tess identifiers.

Point: { default: Int, register: Int }

add(default: Int, register: Int) { default + register }

main() {
    default := 42
    register := 10
    auto := default + register

    p := Point(default = 1, register = 2)

    result := add(p.default, p.register) + auto

    if result == 55 { 0 }
    else { 1 }
}
