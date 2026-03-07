#module main

t_int() {
    x := 1
    x :: Int
}

t_float() {
    x := 1.5
    x :: Float
}

t_invalid_int() {
    x := 2.3
    x :: Int
}

t_invalid_float() {
    x := 2
    x :: Float
}

main() {
    error := 0

    error += if t_int() {0} else {1}
    error += if t_float() {0} else {1}

    error += if t_invalid_int() {1} else {0}
    error += if t_invalid_float() {1} else {0}

    error
}
