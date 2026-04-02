#module MathLib

[[c_export]] factorial(n: CLongLong) -> CLongLong {
    result := 1
    i      := 2
    while i <= n {
        result *= i
        i += 1
    }
    result
}

[[c_export]] gcd(a: CLongLong, b: CLongLong) -> CLongLong {
    while b != 0 {
        t := b
        b = a % b
        a = t
    }
    a
}

[[c_export]] fibonacci(n: CLongLong) -> CLongLong {
    if n <= 1 { n }
    else {
        a := 0
        b := 1
        i := 2
        while i <= n {
            temp := a + b
            a = b
            b = temp
            i += 1
        }
        b
    }
}
