#module Foo

f() { 42 }

#module main

// Error: attempt to monkey-patch by matching mangled name will cause compile error
Foo_f() { 43 }

main() {
  res := Foo.f() - 43
}
