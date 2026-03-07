#module Foo
f1() { 1 }

#module Foo.Bar
// declares intermediate module to allow another level of submodule

#module Foo.Bar.Baz
f2() { 2             }
f3() { Foo.f1() + 10 }

#module main

main() {
    x1 := Foo.f1()
    x2 := Foo.Bar.Baz.f2()
    x3 := Foo.Bar.Baz.f3()
    x1 + x2 + x3 - 14
}
