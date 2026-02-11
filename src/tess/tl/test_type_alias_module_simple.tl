#module Foo
Span[T]: { v: Ptr[T], sz: CSize }

#module Bar
Chars = Foo.Span[CChar]

#module main

main() {
    s: Bar.Chars := Foo.Span(v = null, sz = 0)
    s.sz
}
