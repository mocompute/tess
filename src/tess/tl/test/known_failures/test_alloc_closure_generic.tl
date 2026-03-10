#module main
#import <Alloc.tl>

// A generic allocating closure instantiated with two different types.

make_id[T]() -> (T) -> T {
    f :=[[alloc]](x) { x }
    f
}

main() {
    f     := make_id()
    error := 0
    error += if f(42) == 42 { 0 } else { 1 }
    error += if f(true) == true { 0 } else { 1 }
    error
}
