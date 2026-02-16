#module Foo
f() { 1 }

#module Bar
g() { 2 }

#module main
#alias Foo Bar

main() { Bar.f() }
