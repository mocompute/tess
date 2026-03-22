#module main

main() {
    x: Const := 10

    f := () {
        x = 20
        x
    }
    f()
}
