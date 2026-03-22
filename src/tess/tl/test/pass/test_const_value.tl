#module main
#import <cstdio.tl>

main() {
    // Basic const value
    x: Const[Int] := 42
    c_printf(c"x = %lld\n", x)

    // Const with inference
    y: Const := 100
    c_printf(c"y = %lld\n", y)

    // Passing const to non-const parameter
    c_printf(c"sum = %lld\n", x + y)

    // Shadowing a const binding
    z: Const := 5
    z        := 10
    c_printf(c"z = %lld\n", z)

    0
}
