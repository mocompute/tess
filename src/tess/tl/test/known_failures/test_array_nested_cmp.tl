// Array[Array[Int]].cmp: nested generic specialization generates wrong struct.
// Array[Array[Int]] is emitted with data: Array[Array[Int]]* instead of Array[Int]*.
#module main
#import <Array.tl>

main() {
    inner1 := Array.empty[Int]()
    inner2 := Array.empty[Int]()
    defer inner1.free()
    defer inner2.free()

    inner1.push(1)
    inner2.push(2)

    outer1 := Array.empty[Array[Int]]()
    outer2 := Array.empty[Array[Int]]()
    defer outer1.free()
    defer outer2.free()

    outer1.push(inner1)
    outer2.push(inner2)

    if outer1.cmp(outer2) >= 0 { return 1 }
    0
}
