#module Kw

// Test that C reserved keywords can be used as Tess identifiers.

// Struct with keyword field names
Point: { default: Int, register: Int }

// Enum with keyword variant names
Mode: { auto, register, default }

// Tagged union with keyword variant names
Value: | auto { x: Int }
       | default { x: Int }

add(default: Int, register: Int) { default + register }

#module main

main() {
    error := 0

    // 1. Local variables with keyword names
    default  := 42
    register := 10
    volatile := default + register
    error += if volatile == 52 { 0 } else { 1 }

    // 2. Struct fields with keyword names
    p := Kw.Point(default = 1, register = 2)
    error += if Kw.add(p.default, p.register) == 3 { 0 } else { 1 }

    // 3. Enum variants with keyword names
    m := Kw.Mode.auto
    error += if m == Kw.Mode.auto { 0 } else { 1 }

    // 4. Tagged union with keyword variant names
    v      := Kw.auto(7)
    result := when v {
        a: auto { a.x }
        d: default { d.x }
    }
    error += if result == 7 { 0 } else { 1 }

    // 5. Function parameters with keyword names
    error += if Kw.add(10, 20) == 30 { 0 } else { 1 }

    error
}
