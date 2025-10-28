id(x) { x }
apply(f, x) { f(x) }

main() {
  f1 = id.&
  apply(f1, 0)
}
