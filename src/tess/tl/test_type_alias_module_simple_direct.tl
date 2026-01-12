#module Foo
Span(T) : { v: Ptr(T), sz: CSize }

#module Bar
Chars = Foo.Span(CChar)

#module main
main() {
    // Direct usage: construct using the alias as if it were the type constructor
    s := Bar.Chars(v = null, sz = 0)
    s.sz
}
