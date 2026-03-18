// Known failure: Ptr-receiver trait methods on builtin integer modules crash.
// When a trait defines `mutate(self: Ptr[T])` and `#module Int` implements
// `mutate(self)`, the parameter `self` is inferred as `CLongLong` (by-value)
// instead of `Ptr[CLongLong]`. Dereferencing `self.*` then fails with
// "expected_pointer" or segfaults.

#module Mutate

Mutate[T]: {
    mutate(self: Ptr[T]) -> CInt
}

#module Int
mutate(self) -> CInt { self.* = self.* + 1; 1 }

#module main

modify[T: Mutate](x: Ptr[T]) -> CInt { x.mutate() }

main() {
    a: Int := 10
    error  := if modify(a.&) == 1 { 0 } else { 1 }
    error += if a == 11 { 0 } else { 1 }
    error
}
