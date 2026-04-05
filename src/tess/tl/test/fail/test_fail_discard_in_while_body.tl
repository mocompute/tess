#module main

make_option() -> Option[Int] {
    Some(1)
}

main() {
    i := 0
    while i < 3 {
        make_option()  // discarded in stmt position inside while body — must fail
        i += 1
    }
    0
}
