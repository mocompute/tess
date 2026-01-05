#import <Alloc.tl>

#module Foo

Span(T) : { v: Ptr(T), sz: CSize }

#module Bar

Chars = Foo.Span(CChar)

#module main

main() {

  data : Ptr(CChar) := Alloc.transient_alloc(128 * sizeof(CSignedChar))
  cs := Bar.Chars(v = data, sz = 128)

  0
}
