#module main

test1() {
    error := 0

    good   := Some(123)
    result := case good: Option[Int] {
        x: Some { x.v }
        n: None { 456 }
    }
    error += if result - 123 == 0 {0} else {1}

    good   := Some(0.5)
    result := case good: Option[Float] {
        x: Some { x.v }
        n: None { 45.6 }
    }
    error += if result - 0.5 == 0.0 {0} else {1}


    // Test an alternative to casting at the point of assignment: declare an uninitialized variable of the correct type,
    // and compiler will infer the correct type on a later mutation (assignment)
    nopt: Option[Float] := void
    nopt = None()

    result := case nopt: Option[Float] {
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
