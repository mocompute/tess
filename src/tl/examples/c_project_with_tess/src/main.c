#include "mathlib.h"
#include <stdio.h>

int main(void) {
    tl_init_mathlib();

    printf("factorial(10) = %lld\n", MathLib_factorial(10));
    printf("gcd(48, 18)   = %lld\n", MathLib_gcd(48, 18));
    printf("fibonacci(20) = %lld\n", MathLib_fibonacci(20));

    return 0;
}
