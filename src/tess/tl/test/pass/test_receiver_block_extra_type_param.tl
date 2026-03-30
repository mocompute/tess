// Receiver block where functions introduce additional type parameters
// beyond those inferred from the receiver type.

#module Box

Box[T]: { value: T }

(b: Box[T]): {
    get() -> T {
        b.value
    }

    // U is an additional function-level type parameter.
    map[U](f: (T) -> U) -> Box[U] {
        Box[U](value = f(b.value))
    }
}

#module main

main() {
    error := 0

    b := Box[Int](value = 10)
    error += if b.get() == 10 {0} else {1}

    b2 := b.map((x) { x > 5 })
    error += if b2.get() == true {0} else {1}

    error
}
