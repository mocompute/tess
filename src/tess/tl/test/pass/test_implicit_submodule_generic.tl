// Test generic types in implicit submodules: specialization cache, UFCS, bare Child.Wrapper[T].
#module Parent

#module Parent.Child

Wrapper[T]: { v: T }

wrap[T](x: T) -> Wrapper[T] { Wrapper(v = x) }

unwrap[T](w: Wrapper[T]) -> T { w.v }

#module Parent

// Type position: bare Child.Wrapper[CInt]
make_int(x: CInt) -> Child.Wrapper[CInt] { Child.wrap(x) }

// Expression position: Child.Wrapper(v = ...)
make_bool(b: Bool) -> Child.Wrapper[Bool] { Child.Wrapper(v = b) }

// UFCS on generic submodule type
get_int(w: Child.Wrapper[CInt]) -> CInt { w.unwrap() }

#module main

main() {
    error: CInt := 0

    // Multiple specializations via implicit submodule
    wi := Parent.make_int(42)
    wb := Parent.make_bool(true)

    if wi.v != 42 { error += 1 }
    if !wb.v { error += 1 }

    // UFCS through parent module
    if Parent.get_int(wi) != 42 { error += 1 }

    // Direct UFCS on Child.Wrapper value
    if wi.unwrap() != 42 { error += 1 }

    error
}
