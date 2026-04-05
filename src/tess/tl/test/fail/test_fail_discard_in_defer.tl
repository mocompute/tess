#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    defer {
        make_result()  // discarded Result inside defer block — must fail
        0
    }
    0
}
