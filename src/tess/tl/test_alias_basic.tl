#module Foo
value() { 42 }
add(a, b) { a + b }

#module main
#alias Foo F

main() {
    x := F.value()
    y := F.add(1, 2)
    if x == 42 && y == 3 { 0 } else { 1 }
}
