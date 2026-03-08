#module main

// Two independent recursive generic tagged unions in the same module,
// both multi-specialized. Tests that deferred placeholders from one
// don't interfere with the other via shared type_parse_ctx state.

ListA[T]: | ACons { head: T, tail: Ptr[ListA[T]] }
          | ANil

ListB[T]: | BCons { head: T, tail: Ptr[ListB[T]] }
          | BNil

main() {
    // ListA specialized at Int and Bool
    a1: ListA[Int] := ANil()
    a2: ListA[Bool] := ANil()

    // ListB specialized at Int and Bool
    b1: ListB[Int] := BNil()
    b2: ListB[Bool] := BNil()

    error := 0

    when a1 { c: ACons { error += 1 }  n: ANil { } }
    when a2 { c: ACons { error += 1 }  n: ANil { } }
    when b1 { c: BCons { error += 1 }  n: BNil { } }
    when b2 { c: BCons { error += 1 }  n: BNil { } }

    error
}
