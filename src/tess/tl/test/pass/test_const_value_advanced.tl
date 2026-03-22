#module main
#import <cstdio.tl>

// Const parameter: can't reassign inside, transparent to caller
add_one(x: Const[Int]) {
    x + 1
}

// Generic with const
identity(x: Const) {
    x
}

main() {
    // Const parameter called with plain Int
    c_printf(c"add_one = %lld\n", add_one(5))

    // Generic with Const
    c_printf(c"identity = %lld\n", identity(42))

    // Closure capturing const
    val: Const := 10
    f          := () { val }
    c_printf(c"closure = %lld\n", f())

    0
}
