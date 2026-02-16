#module Outer
// intermediate

#module Outer.Inner
value() { 99 }

#module main
#alias Outer.Inner OI

main() {
    if OI.value() == 99 { 0 } else { 1 }
}
