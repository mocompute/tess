#module Option

T(a): | Some { v: a }
      | None

test1() {
    error := 0

    good   := Option.Some(123)
    result := case good: T {
        x: Some { x.v }
        n: None { 456 }
    }
    error += if result - 123 == 0 {0} else {1}

    good   := Option.Some(0.5)
    result := case good: T {
        x: Some { x.v }
        n: None { 45.6 }
    }
    error += if result - 0.5 == 0.0 {0} else {1}

    error
}

#module main

main() {
    error := 0

    error += Option.test1()

    error
}
