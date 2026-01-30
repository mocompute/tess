#module main

test1() {
    error := 0

    good   := Some(123)
    result := case good: Option {
        x: Some { x.v }
        n: None { 456 }
    }
    error += if result - 123 == 0 {0} else {1}

    good   := Some(0.5)
    result := case good: Option {
        x: Some { x.v }
        n: None { 45.6 }
    }
    error += if result - 0.5 == 0.0 {0} else {1}

    nopt: Option(Float) := None()
    // when using None() variant, the variable must be annotated
    result := case nopt: Option {
        x: Some { x.v }
        n: None { 3.14 }
    }
    error += if result - 3.14 == 0.0 {0} else {1}

    error
}


main() {
    error := 0

    error += test1()

    error
}
