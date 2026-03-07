// Bug: module-prefixed nullary tagged union variant (generic) not auto-invoked.

#module Opt
T[a]: | Has { v: a } | Empty

#module main

main() {
  a := Opt.Has(42)
  b: Opt[Int] := Opt.Empty
  error := 0
  error += when a { s: Has { 0 } n: Empty { 1 } }
  error += when b { s: Has { 1 } n: Empty { 0 } }
  error
}
