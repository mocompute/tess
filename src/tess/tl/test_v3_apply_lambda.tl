apply(f) { f() }

main() {
  afun := () { 0 }
  foo := apply(afun)
  foo
}
