#module main

make_result() -> Result[Int, Int] {
    Ok(42)
}

make_option() -> Option[Int] {
    Some(1)
}

main() {
    when make_option() {
        s: Some {
            make_result()  // discarded inside when arm — must fail
            0
        }
        n: None { 0 }
    }
}
