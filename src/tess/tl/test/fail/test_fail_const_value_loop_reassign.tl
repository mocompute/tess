#module main
#import <Array.tl>

main() {
    arr := Array.empty[Int]()
    arr.push(1)
    arr.push(2)

    for x in arr {
        x = 99
    }

    arr.free()
    0
}
