#module main
#import <Hash.tl>

hash_count(args: ...Hash) -> CSize {
    args.size
}

main() {
    h := hash_count()
    if h != 0 { c_exit(1) }
    0
}
