#module Foo

A: { b: Ptr[B], val: Int }
B: { c: Ptr[C]           }
C: { a: Ptr[A]           }


#module main

main() {
    a := Foo.A(b = null, val = 999)
    b := Foo.B(c = null)
    c := Foo.C(a = a.&)
    a.b            = b.&
    b.c            = c.&
    a.b->c->a->val = 0
    result : CInt := a.b->c->a->val
}
