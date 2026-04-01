// Test else binding with non-diverging else (value-producing).
#module MyRes

MyResult[T, E]: | MyOk { value: T } | MyErr { error: E }

#module main

// Test 1: non-diverging else binding — match case (else not taken)
test_match() -> CInt {
    val: MyRes.MyResult[CInt, CInt] := MyRes.MyOk(10)
    ok: MyOk := val else err { err.error + 100 }
    ok.value - 10
}

// Test 2: non-diverging else binding — no match (else produces value)
test_no_match() -> CInt {
    val: MyRes.MyResult[CInt, CInt] := MyRes.MyErr(5)
    ok: MyOk := val else err { err.error - 5 }
    // When else arm produces a value, the whole expression evaluates to it
    0
}

main() {
    error: CInt := 0
    error += test_match()
    error += test_no_match()
    error
}
