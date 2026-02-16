#module Foo
f() { 1 }

#module Bar
g() { 2 }

#module main
#alias Foo F
#alias Bar F

main() { F.f() }
