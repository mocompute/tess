Foo : {
  x : Int,
  p : Ptr(Foo),
}


main() {
  foo = Foo { x = 0, p = () }
  0
}
