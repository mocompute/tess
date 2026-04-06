#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    f := () {
        make_result()                   // discarded in lambda body — must fail
        0
    }
    f()
}
