#module main
id(x) { x }
apply(f, x) { f(x) }

main() {
  apply(id/1, 0)
}
