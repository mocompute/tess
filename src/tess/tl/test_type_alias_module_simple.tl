#module Foo
Span[T]: { v: Ptr[T], sz: CSize }

#module Bar
Chars = Foo.Span[CChar]

#module main

main() {
    // Via type annotation: construct using original type, annotate with alias
    s1: Bar.Chars := Foo.Span(v = null, sz = 0)

    // Direct usage: construct using the alias as if it were the type constructor
    s2 := Bar.Chars(v = null, sz = 0)

    s1.sz + s2.sz
}
