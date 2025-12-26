#module main

Foo : {
  i : Int,
  f : Float,
}

t() {
  foo := Foo(i = 1, f = 2.5)
  foo.i :: Float
  foo.f :: Int
}

main() {
  t()
  0
}
