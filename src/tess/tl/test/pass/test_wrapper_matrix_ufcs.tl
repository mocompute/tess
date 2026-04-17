// Wrapper-matrix coverage for UFCS method dispatch.
// Each shape resolves to the same method via effective_target.

#module Counter

Counter: { n: CInt }

// Read-only method: takes value.
get(self: Counter) -> CInt { self.n }

// Mutating method: takes pointer.
bump(self: Ptr[Counter]) { self->n = self->n + 1 }

#module main

main() {
    c         := Counter(n = 5)
    cc: Const := Counter(n = 100)

    error: CInt := 0

    // T.get()
    if c.get() != 5 { error += 1 }

    // Const[T].get() (method accepts value; Const is transparent for dispatch).
    if cc.get() != 100 { error += 1 }

    // Mutating: T.bump() via implicit address-of.
    c.bump()
    if c.get() != 6 { error += 1 }

    // Ptr[T].bump() (already a pointer).
    pc: Ptr[Counter] := c.&
    pc.bump()
    if c.get() != 7 { error += 1 }

    error
}
