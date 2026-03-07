#module main

Foo: {
    i: Int,
    f: Float,
}

t() {
    foo := Foo(i = 1, f = 2.5)
    foo.i :: Int && foo.f :: Float
}

main() {
    error := 0
    error += if t() {0} else {1}
    error
}
