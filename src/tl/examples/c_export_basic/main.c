#include "mathlib.h"
#include <stdio.h>

int main(void) {
    tl_init_mathlib();

    printf("factorial(10) = %lld\n", factorial(10));
    printf("gcd(48, 18)   = %lld\n", gcd(48, 18));
    printf("fibonacci(20) = %lld\n", fibonacci(20));

    return 0;
}
