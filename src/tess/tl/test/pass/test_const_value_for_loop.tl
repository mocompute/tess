#module main
#import <cstdio.tl>
#import <Array.tl>

main() {
    arr := Array.empty[Int]()
    arr.push(10)
    arr.push(20)
    arr.push(30)

    // Value iterator: loop var is Const[Int]
    sum := 0
    for x in arr {
        sum = sum + x
    }
    c_printf(c"sum = %lld\n", sum)

    // Pointer iterator: loop var is Const[Ptr[Int]], can mutate pointee
    for p.& in arr {
        p.* = p.* + 1
    }

    sum = 0
    for x in arr {
        sum = sum + x
    }
    c_printf(c"sum after increment = %lld\n", sum)

    arr.free()
    0
}
