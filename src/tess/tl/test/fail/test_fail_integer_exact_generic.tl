#module main

// Generics require exact type match: f(x: T, y: T) called with
// CInt and CShort should fail (T can't unify two different integer types).

f[T](x: T, y: T) -> T { x }

main() {
    a: CInt   := 10
    b: CShort := 5
    f(a, b)
    0
}
