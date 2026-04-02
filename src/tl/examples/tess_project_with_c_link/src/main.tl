#module main
#import "cmathlib.tl"
#import <cstdio.tl>
#link mathlib

main() {
    c_printf(c"factorial(10) = %lld\n", c_factorial(10))
    c_printf(c"gcd(48, 18)   = %lld\n", c_gcd(48, 18))
    c_printf(c"fibonacci(20) = %lld\n", c_fibonacci(20))
    0
}
