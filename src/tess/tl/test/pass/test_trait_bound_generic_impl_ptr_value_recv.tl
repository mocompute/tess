// Generic trait impl on a struct whose field is `Ptr[T]` — but `eq` takes
// its receivers by value (not `Ptr[Const[...]]`). This exercises the
// trait-bound path that maps impl quantifiers to concrete types by
// structurally unifying the impl's receiver-type reference against the
// concrete type (see derive_impl_type_args in infer_specialize.c).
//
// With the naive positional projection over cons_inst->args, the impl's
// quantifier `T_eq` would be bound to `Ptr[CLongLong]` (the field type)
// instead of `CLongLong`, and the trait bound check would reject the impl.

#module GenBoxPtr

GenBoxPtr[T]: { value: Ptr[T] }

eq[T: Eq](a: GenBoxPtr[T], b: GenBoxPtr[T]) -> Bool {
    a.value.* == b.value.*
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
