// Test closure capture-by-value semantics with loop variables.
#module main
#import <Array.tl>
#import <Alloc.tl>

main() {
    error: CInt := 0

    closures := Array.empty[(Int) -> Int]()

    // Create closures in a loop, each capturing current value of i
    i := 0
    while i < 5, i += 1 {
        f := [[alloc, capture(i)]] (x) { x + i }
        Array.push(closures.&, f)
    }

    // Each closure should have captured a different value of i
    // closures[0] captures i=0, closures[1] captures i=1, etc.
    f0 := closures.data.[0]
    f1 := closures.data.[1]
    f2 := closures.data.[2]
    f3 := closures.data.[3]
    f4 := closures.data.[4]
    if f0(10) != 10 { error += 1 }
    if f1(10) != 11 { error += 1 }
    if f2(10) != 12 { error += 1 }
    if f3(10) != 13 { error += 1 }
    if f4(10) != 14 { error += 1 }

    Array.free(closures.&)
    error
}
