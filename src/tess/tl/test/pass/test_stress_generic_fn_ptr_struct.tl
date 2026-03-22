#module main

// Stress test: generic function pointers in structs

// --- Test 1: Generic struct with fn ptr field, multiple specializations ---

Handler[T]: { process: (T) -> T, tag: Int }

double_int(x: Int)  -> Int  { x * 2 }
negate_int(x: Int)  -> Int  { 0 - x }
not_bool  (x: Bool) -> Bool { !x    }

// --- Test 2: Generic struct with TWO fn ptr fields of different signatures ---

DualHandler[T]: { transform: (T) -> T, predicate: (T) -> Bool }

is_positive(x: Int)  -> Bool { x > 0  }
is_true    (x: Bool) -> Bool { x      }
add_ten    (x: Int)  -> Int  { x + 10 }
flip       (x: Bool) -> Bool { !x     }

// --- Test 3: Generic struct with fn ptr that takes/returns same generic as another field ---

Wrapper[T]: { value: T, modify: (T) -> T }

apply_wrapper[T](w: Wrapper[T]) -> T {
    w.modify(w.value)
}

// --- Test 4: Pass generic struct with fn ptr to generic function that invokes it ---

invoke_handler[T](h: Handler[T], x: T) -> T {
    h.process(x)
}

// --- Test 5: Multiple instances of same generic struct type with different fn ptrs ---

triple_int(x: Int) -> Int { x * 3 }

main() {
    error := 0

    // Test 1a: Handler[Int] with double
    h1 := Handler(process = double_int/1, tag = 1)
    r1 := h1.process(5)
    error += if r1 == 10 { 0 } else { 1 }

    // Test 1b: Handler[Int] with negate
    h2 := Handler(process = negate_int/1, tag = 2)
    r2 := h2.process(7)
    error += if r2 == -7 { 0 } else { 1 }

    // Test 1c: Handler[Bool] specialization
    h3 := Handler(process = not_bool/1, tag = 3)
    r3 := h3.process(true)
    error += if r3 == false { 0 } else { 1 }

    // Test 2a: DualHandler[Int]
    dh1 := DualHandler(transform = add_ten/1, predicate = is_positive/1)
    r4  := dh1.transform(5)
    error += if r4 == 15 { 0 } else { 1 }
    r5 := dh1.predicate(r4)
    error += if r5 == true { 0 } else { 1 }

    // Test 2b: DualHandler[Bool]
    dh2 := DualHandler(transform = flip/1, predicate = is_true/1)
    r6  := dh2.transform(false)
    error += if r6 == true { 0 } else { 1 }
    r7 := dh2.predicate(r6)
    error += if r7 == true { 0 } else { 1 }

    // Test 3: Wrapper with value and fn ptr on same type
    w1 := Wrapper(value = 100, modify = double_int/1)
    r8 := apply_wrapper(w1)
    error += if r8 == 200 { 0 } else { 1 }

    w2 := Wrapper(value = true, modify = not_bool/1)
    r9 := apply_wrapper(w2)
    error += if r9 == false { 0 } else { 1 }

    // Test 4: Pass Handler to generic function
    r10 := invoke_handler(h1, 20)
    error += if r10 == 40 { 0 } else { 1 }

    r11 := invoke_handler(h3, false)
    error += if r11 == true { 0 } else { 1 }

    // Test 5: Multiple instances with different fn ptrs
    handlers_a := Handler(process = double_int/1, tag = 10)
    handlers_b := Handler(process = triple_int/1, tag = 20)
    handlers_c := Handler(process = negate_int/1, tag = 30)

    sum := handlers_a.process(3) + handlers_b.process(3) + handlers_c.process(3)
    // 6 + 9 + (-3) = 12
    error += if sum == 12 { 0 } else { 1 }

    // Also invoke them through the generic function
    sum2 := invoke_handler(handlers_a, 4) + invoke_handler(handlers_b, 4) + invoke_handler(handlers_c, 4)
    // 8 + 12 + (-4) = 16
    error += if sum2 == 16 { 0 } else { 1 }

    error
}
