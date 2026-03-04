#module Foo
f() { 1 }

#module Bar
g() { 2 }

#module main
#alias F Foo
#alias F Bar

main() { F.f() }
