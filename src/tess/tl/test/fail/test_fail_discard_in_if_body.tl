#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    if 1 == 1 {
        make_result()                   // discarded in stmt position inside if body — must fail
        0
    } else { 0 }
}
