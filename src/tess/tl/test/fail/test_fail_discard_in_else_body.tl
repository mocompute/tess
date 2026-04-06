#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    if 1 == 2 {
        0
    } else {
        make_result()  // discarded in else body — must fail
        0
    }
}
