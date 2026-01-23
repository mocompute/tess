#module main
add1(x)  { x + 1 }
foo(f, x) { f(x) }

main() {
  fp := add1/1
  foo(fp, -1)
}
