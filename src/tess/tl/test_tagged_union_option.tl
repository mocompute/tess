#module Option

T(a) = | Some { v: a }
       | None

test1() {
    error := 0

    good := T_Some(v = 123)

    result := case good: T {
        x: Some { x.v }
        n: None { 456 }
    }

    error += if result - 123 == 0 {0} else {1}

    error
}

#module main

main() {
    error := 0

    error += Option.test1()

    error
}
