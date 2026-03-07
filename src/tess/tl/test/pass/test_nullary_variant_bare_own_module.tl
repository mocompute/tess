// Bare nullary variant inside its own (non-main) module.

#module Foo

Op: | A { v: Int } | B

make_b() -> Op {
  B
}

check() -> Int {
  a := A(42)
  b := B
  error := 0
  error += when a { x: A { 0 } y: B { 1 } }
  error += when b { x: A { 1 } y: B { 0 } }
  error
}

#module main

main() -> Int {
  Foo.check()
}
