#module Foo
f() { 1 }

#module main
#alias Foo F
#alias F G

main() { G.f() }
