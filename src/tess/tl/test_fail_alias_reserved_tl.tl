#module Foo
f() { 1 }

#module main
#alias _tl_foo Foo

main() { _tl_foo.f() }
