#module Foo
f() { 1 }

#module main
#alias Foo Foo

main() { Foo.f() }
