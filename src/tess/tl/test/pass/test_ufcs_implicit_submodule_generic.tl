// Test UFCS resolution on generic types from implicit submodules.
#module Parent

#module Parent.Child
Data:                      { x: CInt           }
create(v: CInt) -> Data    { Data(x = v)       }
transform(d: Data) -> Data { Data(x = d.x * 2) }
get_value(d: Data) -> CInt { d.x               }

Wrapper[T]:                   { v: T           }
wrap[T](x: T) -> Wrapper[T]   { Wrapper(v = x) }
unwrap[T](w: Wrapper[T]) -> T { w.v            }

#module Parent

// UFCS chain through implicit submodule
chain_test(v: CInt) -> CInt {
    Child.create(v).transform().get_value()
}

// UFCS on generic submodule type
unwrap_int(w: Child.Wrapper[CInt]) -> CInt { w.unwrap() }

#module main

main() {
    error: CInt := 0

    // UFCS chain
    if Parent.chain_test(5) != 10 { error += 1 }

    // UFCS on generic type from implicit submodule
    w := Parent.Child.wrap(99)
    if Parent.unwrap_int(w) != 99 { error += 1 }

    // Direct UFCS on Child.Data
    d := Parent.Child.create(7)
    if d.transform().get_value() != 14 { error += 1 }

    error
}
