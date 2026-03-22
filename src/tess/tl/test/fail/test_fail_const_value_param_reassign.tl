#module main

foo(x: Const[Int]) {
    x = 10
    x
}

main() {
    foo(42)
}
