#module Foo
f1() { 1 }

// Foo.Bar.Baz without declaring Foo.Bar first
#module Foo.Bar.Baz
f2() { 2 }

#module main
main() { Foo.f1() + Foo.Bar.Baz.f2() }
