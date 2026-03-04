#module Outer
// intermediate

#module Outer.Inner
value() { 99 }

#module main
#alias OI Outer.Inner

main() {
    if OI.value() == 99 { 0 } else { 1 }
}
