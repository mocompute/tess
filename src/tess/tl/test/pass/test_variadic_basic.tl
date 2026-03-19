// Test basic variadic function definition and call using the Hash trait.


#module main
#import <Hash.tl>

hash_all(args: ...Hash.Hash) -> CSize {
    args.size
}

main() {
    h := hash_all(42, 100)
    if h != 2 { c_exit(1) }
    0
}
