#module main

// Generic tagged union: Option-like
Opt[T]: | Val { value: T }
        | Nope

// --- map: apply function to value inside tagged union ---
opt_map[T, U](o: Opt[T], f) -> Opt[U] {
    when o {
        s: Val { Val(f(s.value)) }
        n: Nope { Nope() }
    }
}

// --- flatmap / monadic bind ---
opt_flatmap[T, U](o: Opt[T], f) -> Opt[U] {
    when o {
        s: Val { f(s.value) }
        n: Nope { Nope() }
    }
}

// --- unwrap with default ---
opt_unwrap[T](o: Opt[T], def: T) -> T {
    when o {
        s: Val { s.value }
        n: Nope { def }
    }
}

// --- concrete helper functions ---
double_int(x: Int) -> Int { x * 2  }
add_ten   (x: Int) -> Int { x + 10 }

safe_div(x: Int) -> Opt[Int] {
    if x == 0 { Nope() } else { Val(100 / x) }
}

int_to_positive(x: Int) -> Opt[Int] {
    if x > 0 { Val(x) } else { Nope() }
}

// --- call a function returning Opt, pattern match the result ---
call_and_unwrap(f, arg: Int, def: Int) -> Int {
    result: Opt[Int] := f(arg)
    when result {
        s: Val { s.value }
        n: Nope { def }
    }
}

// --- compose two Int->Int functions, second returns Opt ---
compose_and_call(f, g, x: Int) -> Opt[Int] {
    g(f(x))
}

main() {
    error := 0

    // Test 1: basic map over Val
    v1 := Val(5)
    r1 := opt_map(v1, double_int/1)
    error += if opt_unwrap(r1, 0) == 10 {0} else {1}

    // Test 2: map over Nope
    n1: Opt[Int] := Nope()
    r2           := opt_map(n1, double_int/1)
    error += if opt_unwrap(r2, 0) == 0 {0} else {1}

    // Test 3: flatmap with success
    v2 := Val(5)
    r3 := opt_flatmap(v2, safe_div/1)
    error += if opt_unwrap(r3, 0) == 20 {0} else {1}

    // Test 4: flatmap with failure (division by zero path)
    v3 := Val(0)
    r4 := opt_flatmap(v3, safe_div/1)
    error += if opt_unwrap(r4, -1) == -1 {0} else {1}

    // Test 5: chain map then flatmap
    v4 := Val(3)
    r5 := opt_map(v4, add_ten/1)
    r6 := opt_flatmap(r5, int_to_positive/1)
    error += if opt_unwrap(r6, 0) == 13 {0} else {1}

    // Test 6: chain flatmap then map
    v5 := Val(10)
    r7 := opt_flatmap(v5, safe_div/1)
    r8 := opt_map(r7, double_int/1)
    error += if opt_unwrap(r8, 0) == 20 {0} else {1}

    // Test 7: call_and_unwrap with function pointer
    r9 := call_and_unwrap(safe_div/1, 4, -1)
    error += if r9 == 25 {0} else {1}

    // Test 8: call_and_unwrap with Nope result
    r10 := call_and_unwrap(safe_div/1, 0, -1)
    error += if r10 == -1 {0} else {1}

    // Test 9: compose_and_call
    r11 := compose_and_call(double_int/1, safe_div/1, 3)
    error += if opt_unwrap(r11, 0) == 16 {0} else {1}

    // Test 10: multiple map calls chained
    v6  := Val(1)
    r12 := opt_map(v6, double_int/1)
    r13 := opt_map(r12, double_int/1)
    r14 := opt_map(r13, add_ten/1)
    error += if opt_unwrap(r14, 0) == 14 {0} else {1}

    // Test 11: flatmap chain (monadic pipeline)
    v7  := Val(2)
    r15 := opt_flatmap(v7, safe_div/1)
    r16 := opt_flatmap(r15, int_to_positive/1)
    error += if opt_unwrap(r16, 0) == 50 {0} else {1}

    // Test 12: map with lambda
    v8  := Val(7)
    r17 := opt_map(v8, (x: Int) { x * x })
    error += if opt_unwrap(r17, 0) == 49 {0} else {1}

    // Test 13: generic function returning TU passed as function pointer
    r18 := call_and_unwrap(int_to_positive/1, 42, -1)
    error += if r18 == 42 {0} else {1}

    // Test 14: generic function returning TU passed as fp, negative case
    r19 := call_and_unwrap(int_to_positive/1, -5, -1)
    error += if r19 == -1 {0} else {1}

    // Test 15: compose_and_call with add_ten and safe_div
    r20 := compose_and_call(add_ten/1, safe_div/1, 0)
    // add_ten(0) = 10, safe_div(10) = Val(10)
    error += if opt_unwrap(r20, 0) == 10 {0} else {1}

    error
}
