#module main

Foo[a]: { value: a, other: Ptr[Bar[a]] }
Bar[a]: { value: a, other: Ptr[Foo[a]] }

identity[T](x: T) -> T { x }

main() {
    f := Foo(value = 1, other = null)
    w := identity[Foo[Int]](f)
    0
}
