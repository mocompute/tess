// Variadic and non-variadic with same name and matching arity — must fail.

#module main
#import <Hash.tl>

hash_all(x: CInt, args: ...Hash) -> CSize { args.size }
hash_all(a: CInt, b: CInt)       -> CSize { 0         }

main() {
    hash_all(1, 2)
    0
}
