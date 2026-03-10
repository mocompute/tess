#module Foo
value() { 55 }

#module Bar
other() { 33 }

#module FileOne
#alias F Foo
use_alias() { F.value() }

// Simulate a file boundary: aliases are reset
#unity_file test_alias_file_boundary_part2.tl

#module FileTwo
// After #unity_file, alias F is cleared and can be reused for a different module.
#alias F Bar
use_bar() { F.other() }

#module main

main() {
    x := FileOne.use_alias()
    y := FileTwo.use_bar()
    if x == 55 && y == 33 { 0 } else { 1 }
}
