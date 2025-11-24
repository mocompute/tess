#module main

foo(x: Type) { sizeof(x) }

main() {
  foo(Int) - 8
}
