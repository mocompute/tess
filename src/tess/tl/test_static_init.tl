#module Foo

global := 123

#module main

val := 456

main() {
    val - Foo.global - 333
}
