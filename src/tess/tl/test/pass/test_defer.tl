#module main

// break with defer (simple)
f1() {
    x := 1
    while true {
        x       = x + 1
        defer x = 5

        break
    }

    x - 5
}

// break with defer block
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

// return from inside a while with defer (capture-first: returns pre-defer value)
f_return_while() {
    x := 10
    while true {
        defer x = 0
        return x
    }
    0
}

// continue with defer in a counted loop
f_continue() {
    sum := 0
    i   := 0
    while i < 3, i = i + 1 {
        defer sum = sum + 1
        continue
    }
    sum - 3
}

// multiple defers in one body (LIFO ordering)
f_lifo() {
    x := 0
    while true {
        defer x = x * 10 + 1
        defer x = x * 10 + 2
        defer x = x * 10 + 3
        break
    }
    x - 321
}

// nested while loops with defers at different levels + break
f_nested() {
    x := 0
    while true {
        defer x = x + 1
        while true {
            defer x = x + 10
            break
        }
        break
    }
    x - 11
}

// return from deeply nested scope with defers at multiple levels
f_deep_return() {
    x := 0
    while true {
        defer x = x + 1
        while true {
            defer x = x + 10
            return x
        }
    }
    0
}

// implicit return with defers on the function body (capture-first)
f_implicit() {
    x := 42
    defer x = 0
    x
}


main() {
    error := 0

    error += f1()
    error += f2()
    error += f_return_while() - 10
    error += f_continue()
    error += f_lifo()
    error += f_nested()
    error += f_deep_return()
    error += f_implicit() - 42

    error
}
