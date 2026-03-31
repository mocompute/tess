#import <Alloc.tl>

#module Foo

Span[T]: { v: Ptr[T], sz: CSize }

#module main

main() {

    data: CString := Alloc.context.transient.alloc(128zu * sizeof[CSignedChar]())

    // regression test: cs was typed using unspecialized Foo.Span rather than specialized type
    cs := Foo.Span(v = data, sz = 128zu)

    0
}
