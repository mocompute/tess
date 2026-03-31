#module main
#import <Array.tl>
#import <Slice.tl>

main() {
    arr := Array.init(0, 4)
    i := 1
    for x.& in arr {
        x.* = i
        i = i + 1
    }

    s := arr.slice(0, 4)

    // Value iteration over Slice -- no explicit module needed
    sum := 0
    for x in s {
        sum = sum + x
    }

    if sum != 10 { 1 } else { 0 }
}
