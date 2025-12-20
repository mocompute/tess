#module main

foo() -> Ptr(any) { return null }

main() {
  if null == foo() { 0 } else { 1 }
}
