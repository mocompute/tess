#module main
#import <Alloc.tl>

// [[alloc]] on a closure with zero captures should work.
// No capture list is needed when there are no free variables.
// Currently fails: codegen produces undeclared identifier for the closure binding.

make_doubler() -> (Int) -> Int {
    f := [[alloc]] (x) { x + x }
    f
}

main() {
    f     := make_doubler()
    error := 0
    error += if f(21) == 42 { 0 } else { 1 }
    error += if f(0) == 0 { 0 } else { 1 }
    error
}
