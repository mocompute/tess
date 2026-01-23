#module main
id(x) { x }
apply(f, x) { f(x) }

main() {
  f1 := id/1
  f1 := id/1 // shadow, the prior value is unspecialised and must not be emitted
  apply(f1, 0)
}
