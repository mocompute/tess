// Ensure module-prefixed nullary variant works with non-T type names
// (no auto-collapse interaction).

#module Opt
MyOption: | Has { v: Int } | Empty

#module main

main() {
    a     := Opt.Has(42)
    b     := Opt.Empty
    error := 0
    error += when a { s: Has { 0 } n: Empty { 1 } }
    error += when b { s: Has { 1 } n: Empty { 0 } }
    error
}
