#module main
#import <Alloc.tl>

make_id[T]() -> (T) -> T {
    f := [[alloc]] (x) { x }
    f
}

main() {
    f_int  := make_id[Int]()
    f_bool := make_id[Bool]()
    error  := 0
    error += if f_int(42) == 42 { 0 } else { 1 }
    error += if f_bool(true) == true { 0 } else { 1 }
    error
}
