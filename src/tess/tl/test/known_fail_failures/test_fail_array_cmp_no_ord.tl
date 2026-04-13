#module main
#import <Array.tl>

// A type with no cmp or eq — does not conform to Ord.
Thing: { val: Int }

main() {
    a := Array.empty[Thing]()
    b := Array.empty[Thing]()
    a.cmp(b)
}
