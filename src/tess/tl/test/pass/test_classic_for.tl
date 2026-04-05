#module main
#import <Print.tl>

println = Print.println

main() -> CInt {
    // Basic classic for loop: 0+1+2+3+4 = 10
    sum: Int := 0
    for i := 0, i < 5, i += 1 {
        sum += i
    }
    println(sum)

    // break: loop exits at j==3, so n==3
    n := 0
    for j := 0, j < 100, j += 1 {
        if j == 3 { break }
        n += 1
    }
    println(n)

    // continue: update must still run; even sum 0+2+4+6+8 = 20
    evens: Int := 0
    for k := 0, k < 10, k += 1 {
        if k % 2 != 0 { continue }
        evens += k
    }
    println(evens)

    // nested loops: 3*3 = 9
    result: Int := 0
    for a := 0, a < 3, a += 1 {
        for b := 0, b < 3, b += 1 {
            result += 1
        }
    }
    println(result)

    // count down from 10: 10 iterations
    count: Int := 0
    for x := 10, x > 0, x -= 1 {
        count += 1
    }
    println(count)

    0
}
