#module main

FooUnion : {
  | i : Int
  | f : Float
}

main() {
  u := FooUnion { i = 0 }
  0
}
