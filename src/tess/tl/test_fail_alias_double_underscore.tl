#module Foo
f() { 1 }

#module main
#alias Foo my__alias

main() { my__alias.f() }
