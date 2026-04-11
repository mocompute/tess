// Generic trait impl with an extra level of indirection:
// `GenBoxPtr[T]` stores a `Ptr[T]`, and `GenBoxPtr.eq[T: Eq]` compares
// the pointed-to values. Generic code using an `Eq` bound on
// `GenBoxPtr[Int]`/`GenBoxPtr[Float]` should dispatch to this impl.

#module GenBoxPtr

GenBoxPtr[T]: { value: Ptr[T] }

eq[T: Eq](a: Ptr[Const[GenBoxPtr[T]]], b: Ptr[Const[GenBoxPtr[T]]]) -> Bool {
    a->value.* == b->value.*
}


#module main

bounds_are_equal[T: Eq](a: T, b: T) -> Bool {
    a == b
}

main() {
    error := 0

    // Specialisation 1: GenBoxPtr[Int]
    i1: Int := 42
    i2: Int := 42
    i3: Int := 99

    p1 := GenBoxPtr(value = i1.&)
    p2 := GenBoxPtr(value = i2.&)
    p3 := GenBoxPtr(value = i3.&)

    error += if bounds_are_equal(p1, p2) { 0 } else { 1 }
    error += if bounds_are_equal(p1, p3) { 1 } else { 0 }

    // Specialisation 2: GenBoxPtr[Float]
    f1: Float := 1.5
    f2: Float := 1.5
    f3: Float := 2.5

    pf1 := GenBoxPtr(value = f1.&)
    pf2 := GenBoxPtr(value = f2.&)
    pf3 := GenBoxPtr(value = f3.&)

    error += if bounds_are_equal(pf1, pf2) { 0 } else { 1 }
    error += if bounds_are_equal(pf1, pf3) { 1 } else { 0 }

    error
}
