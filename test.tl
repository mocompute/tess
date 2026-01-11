#import <Alloc.tl>

#module Foo

Span(T) : { v: Ptr(T), sz: CSize }

#module main

Chars = Span(CUnsignedChar)


main() {

  data : Ptr(CUnsignedChar) := Alloc.transient_alloc(128 * sizeof(CUnsignedChar))
  cs := Chars(v = data, sz = 128)

  0
}
