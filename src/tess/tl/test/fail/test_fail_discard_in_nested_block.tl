#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    {
        make_result()                   // discarded inside nested block — must fail
        0
    }
    0
}
