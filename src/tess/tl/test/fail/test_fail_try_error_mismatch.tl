#module main

// try where error types DON'T match should fail.
// make_result returns Result[CInt, CInt], but function returns Result[Void, String].

make_result(x: CInt) -> Result[CInt, CInt] {
    Ok(x)
}

bad_try() -> Result[Void, String] {
    try make_result(42)
    Ok(void)
}

main() {
    0
}
