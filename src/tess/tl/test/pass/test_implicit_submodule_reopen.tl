// Test interaction: implicit submodule resolution across module reopenings.
#module Parent

#module Parent.Child
Data:                   { x: CInt     }
create(v: CInt) -> Data { Data(x = v) }

#module Parent
// First use of implicit Child
first(v: CInt) -> Child.Data { Child.create(v) }

#module Parent.Child
// Reopen submodule itself: add more functions
double(d: Data) -> Data { Data(x = d.x * 2) }

#module Parent
// Reopened parent: implicit Child still works, including new functions
second(v: CInt) -> CInt {
    d  := Child.create(v)
    d2 := Child.double(d)
    d2.x
}

#module main

main() {
    error: CInt := 0

    d := Parent.first(10)
    if d.x != 10 { error += 1 }

    if Parent.second(21) != 42 { error += 1 }

    error
}
