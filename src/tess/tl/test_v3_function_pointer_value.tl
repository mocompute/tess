add1(x)  { x + 1 }
foo(f, x) { f(x) }

main() {
  fp := add1
  foo(fp, -1)
}
