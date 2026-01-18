#module main

foo() {
  error := 0
  error = error + if true {99} else {1}
  error
}

main() {
  foo() - 99
}
