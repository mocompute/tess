#import <Alloc.tl>

#module Foo

Span(T): { v: Ptr[T], sz: CSize }

#module main

main() {

    data: CString := Alloc.transient_alloc(128 * sizeof(CSignedChar))

    // regression test: cs was typed using unspecialized Foo.Span rather than specialized type
    cs := Foo.Span(v = data, sz = 128)

    0
}
