id(x) { x }
apply(f, x) { f(x) }

main() {
  f1 = id
  f1 = id // shadow, the prior value is unspecialised and must not be emitted
  apply(f1, 0)
}
