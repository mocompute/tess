#module main
#import <Hash.tl>

count_with_base(base: CSize, args: ...Hash.Hash) -> CSize {
    base + args.size
}

main() {
    r := count_with_base(10, 1, 2, 3)
    if r != 13 { c_exit(1) }
    0
}
