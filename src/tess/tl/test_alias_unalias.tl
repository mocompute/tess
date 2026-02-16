#module Foo
value() { 10 }

#module main
#alias Foo F
use_alias() { F.value() }

#unalias F
use_original() { Foo.value() }

main() {
    x := use_alias()
    y := use_original()
    if x == 10 && y == 10 { 0 } else { 1 }
}
