#module main

// Annotated let bindings can explicitly cast away const (the language's cast mechanism).

main() {
    x                   := 42
    cp: Ptr[Const[Int]] := x.&
    mp: Ptr[Int]        := cp           // explicit cast: strip const via let binding
    mp.* = 99
    if x == 99 {0} else {1}
}
