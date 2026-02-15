#module main

f1() {
    x := 1
    while true {
        x       = x + 1
        defer x = 5

        break
    }

    x - 5
}

f2 () {
    x := 1
    while true {
        x = x + 1

        defer {
            a := 1
            x = 2
            x = 10
        }
        break
    }

    x - 10
}


main() {
    error := 0

    error += f1()
    error += f2()

    error
}
