#module Foo
f() { 1 }

#module main
#alias Foo c_foo

main() { c_foo.f() }
