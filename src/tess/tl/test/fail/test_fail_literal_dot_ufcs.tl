#module main
#import <Hash.tl>

// 42.hash() should fail: the dot is consumed as a decimal point by the tokenizer.
// The compiler should hint to parenthesize the literal: (42).hash()
main() {
    h := 42.hash()
    0
}
