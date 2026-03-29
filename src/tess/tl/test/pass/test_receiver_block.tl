// Basic receiver block: single parameter, non-generic.
// Verifies desugaring produces correct functions and UFCS dispatch works.

#module Counter

Counter: { value: Int }

(self: Ptr[Counter]): {
    get() -> Int {
        self->value
    }

    set(v: Int) -> Void {
        self->value = v
    }

    increment() -> Void {
        self->value = self->value + 1
    }
}

#module main

main() {
    error := 0
    c     := Counter(value = 0)

    error += if c.get() == 0 {0} else {1}

    c.set(42)
    error += if c.get() == 42 {0} else {1}

    c.increment()
    error += if c.get() == 43 {0} else {1}

    error
}
