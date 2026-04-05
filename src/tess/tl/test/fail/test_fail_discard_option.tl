#module main

make_option() -> Option[Int] {
    Some(1)
}

main() {
    make_option()  // discarded Option — must fail
    0
}
