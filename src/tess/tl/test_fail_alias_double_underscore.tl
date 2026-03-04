#module Foo
f() { 1 }

#module main
#alias my__alias Foo

main() { my__alias.f() }
