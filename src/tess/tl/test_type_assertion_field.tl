#module main

Foo : {
  i : Int,
  f : Float,
}

t() {
  foo := Foo(i = 1, f = 2.5)
  foo.i :: Int
  foo.f :: Float
}

main() {
  t()
  0
}
