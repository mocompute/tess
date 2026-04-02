#include "mathlib.h"

long long factorial(long long n) {
    long long result = 1;
    for (long long i = 2; i <= n; i++)
        result *= i;
    return result;
}

long long gcd(long long a, long long b) {
    while (b != 0) {
        long long t = b;
        b = a % b;
        a = t;
    }
    return a;
}

long long fibonacci(long long n) {
    if (n <= 1) return n;
    long long a = 0, b = 1;
    for (long long i = 2; i <= n; i++) {
        long long temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}
