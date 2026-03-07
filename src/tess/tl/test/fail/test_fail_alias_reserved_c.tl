#module Foo
f() { 1 }

#module main
#alias c_foo Foo

main() { c_foo.f() }
