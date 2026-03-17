#module main
#import <Array.tl>

main() {
    arr := Array.empty[Int]()
    arr.nonexistent_function()          // no such function in Array module
    0
}
