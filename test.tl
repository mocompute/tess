#module main

nil_return() { return () }

main() {
  if true { return nil_return() }
  1
}
