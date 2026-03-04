#module Foo
f() { 1 }

#module Bar
g() { 2 }

#module main
#alias Bar Foo

main() { Bar.f() }
