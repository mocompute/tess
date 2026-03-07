#module Foo
f() { 1 }

#module main
#alias F Foo
#alias G F

main() { G.f() }
