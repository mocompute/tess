// A variadic trait function must return a concrete type — returning T must fail.

#module Identity

Identity[T]: {
    identity(x: T) -> T
}

#module main

bar(args: ...Identity.Identity) -> CInt { 0 }

main() {
    bar(1, 2)
    0
}
