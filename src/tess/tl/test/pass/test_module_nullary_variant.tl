// Bug: module-prefixed nullary tagged union variant not auto-invoked.
// Opt.Empty should produce a value, not a function reference.

#module Opt
T: | Has { v: Int } | Empty

#module main

main() {
    a     := Opt.Has(42)
    b     := Opt.Empty
    error := 0
    error += when a { s: Has { 0 } n: Empty { 1 } }
    error += when b { s: Has { 1 } n: Empty { 0 } }
    error
}
