#module MyOption

T[a]: | Has { v: a }
      | Empty

test1() {
    error := 0

    good   := MyOption.Has(123)
    result := when good {
        x: Has { x.v }
        n: Empty { 456 }
    }
    error += if result - 123 == 0 {0} else {1}

    good   := MyOption.Has(0.5)
    result := when good {
        x: Has { x.v }
        n: Empty { 45.6 }
    }
    error += if result - 0.5 == 0.0 {0} else {1}

    error
}

#module main

main() {
    error := 0

    error += MyOption.test1()

    error
}
