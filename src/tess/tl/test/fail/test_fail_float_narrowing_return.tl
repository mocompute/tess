#module main

// Narrowing: Function returns CDouble, declared -> CFloat should fail.

get_value() -> CDouble { 3.14 }

narrow_return() -> CFloat {
    get_value()
}

main() {
    narrow_return()
    0
}
