#module main

// Narrowing: Function returns Int, declared -> CInt should fail.

get_value() -> Int { 42 }

narrow_return() -> CInt {
    get_value()
}

main() {
    narrow_return()
    0
}
