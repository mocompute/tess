#module main
#import <Array.tl>

main() {
    arr := Array.empty[Int]()
    arr.push("hello")                   // type error: String vs Int
    0
}
