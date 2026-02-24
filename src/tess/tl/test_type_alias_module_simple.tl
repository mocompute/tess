#module Foo
Span[T]: { v: Ptr[T], sz: CSize }

#module Bar
Chars = Foo.Span[CChar]

#module main

main() {
    // Via type annotation: construct using original type, annotate with alias
    s1: Bar.Chars := Foo.Span(v = null, sz = 0zu)

    // Direct usage: construct using the alias as if it were the type constructor
    s2 := Bar.Chars(v = null, sz = 0zu)

    // Verify both sz fields are accessible and unsigned
    result: CSize := s1.sz + s2.sz
    if result != 0zu {1} else {0}
}
