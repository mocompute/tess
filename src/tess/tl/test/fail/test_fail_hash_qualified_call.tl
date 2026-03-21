#module main
#import <Hash.tl>

// Hash.hash(42) should fail: hash() is a trait method, not a function in module Hash.
// The compiler should suggest using UFCS instead.
main() {
    h := Hash.hash(42)
    0
}
