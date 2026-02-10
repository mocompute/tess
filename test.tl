#module main

foo[T, U](x: T) {
    if T :: Int { x } else { 1 }
}

main() {

    foo[Int](0)

}
