#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

main() {
    x := 1
    if x == 0 {
        0
    } else if x == 1 {
        make_result()                   // discarded in else-if branch — must fail
        0
    } else {
        0
    }
}
