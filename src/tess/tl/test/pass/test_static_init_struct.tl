#module Foo

Bar: { x: Int }

the_bar := Bar(x = 123)

#module main

main() {
    result: CInt := Foo.the_bar.x - 123
    result
}
