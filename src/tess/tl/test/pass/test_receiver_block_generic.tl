// Receiver block with generic type parameter inference.

#module Box

Box[T]: { value: T }

(self: Ptr[Box[T]]): {
    get() -> T {
        self->value
    }

    set(v: T) -> Void {
        self->value = v
    }
}

#module main

main() {
    error := 0

    b := Box[Int](value = 10)
    error += if b.get() == 10 {0} else {1}

    b.set(42)
    error += if b.get() == 42 {0} else {1}

    error
}
