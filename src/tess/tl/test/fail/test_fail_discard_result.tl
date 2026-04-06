#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    make_result()                       // discarded Result — must fail
    0
}
