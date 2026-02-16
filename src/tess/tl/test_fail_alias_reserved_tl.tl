#module Foo
f() { 1 }

#module main
#alias Foo _tl_foo

main() { _tl_foo.f() }
