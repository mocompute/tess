#module main
#import <Array.tl>

main() {
    arr := Array.empty[Int]()
    arr.push("hello")   // type error: Str vs Int
    0
}
