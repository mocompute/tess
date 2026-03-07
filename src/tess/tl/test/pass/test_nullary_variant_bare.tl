// Bug: bare nullary tagged union variant not auto-invoked in same module.
// `B` should produce a value, not a function reference.

#module main

Op: | A { v: Int } | B

main() {
  a := A(42)
  b := B
  error := 0
  error += when a { x: A { 0 } y: B { 1 } }
  error += when b { x: A { 1 } y: B { 0 } }
  error
}
